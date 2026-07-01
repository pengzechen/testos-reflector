#!/usr/bin/env python3
"""
Quantize a LLaMA safetensors model to INT8 and produce .tlm + .tkn files
for TestOS bare-metal inference.

Usage:
  python quantize_model.py --model_dir ../rknpu-llama/models/story --output_dir .

Produces:
  story.tlm   - INT8 weights + config + OUT_CVT params + RoPE tables
  story.tkn   - BPE tokenizer binary
"""

import argparse
import json
import math
import struct
import numpy as np
from pathlib import Path

# Tensor IDs for the .tlm format
TID_EMBEDDING     = 0
TID_RMS_ATT_W     = 1
TID_WQ            = 2
TID_WK            = 3
TID_WV            = 4
TID_WO            = 5
TID_RMS_FFN_W     = 6
TID_W_GATE        = 7
TID_W_UP          = 8
TID_W_DOWN        = 9
TID_RMS_OUT_W     = 10
TID_LM_HEAD       = 11

# Which tensor IDs are linear layers (need OUT_CVT params)
LINEAR_TIDS = {TID_WQ, TID_WK, TID_WV, TID_WO, TID_W_GATE, TID_W_UP, TID_W_DOWN, TID_LM_HEAD}


def quantize_symmetric(tensor_f32):
    """Per-tensor symmetric quantization: float -> int8."""
    abs_max = np.max(np.abs(tensor_f32))
    if abs_max < 1e-10:
        return np.zeros_like(tensor_f32, dtype=np.int8), 1.0
    scale = abs_max / 127.0
    quantized = np.clip(np.round(tensor_f32 / scale), -128, 127).astype(np.int8)
    return quantized, scale


def find_scale_shift(multiplier):
    """Find uint16 scale and shift such that scale >> shift ≈ multiplier."""
    best_shift = 0
    best_scale = 1
    best_error = float('inf')

    for shift in range(0, 32):
        ideal_scale = multiplier * (1 << shift)
        if ideal_scale > 65535:
            break
        int_scale = max(1, min(65535, int(round(ideal_scale))))
        actual = int_scale / (1 << shift)
        error = abs(actual - multiplier) / max(abs(multiplier), 1e-10)
        if error < best_error:
            best_error = error
            best_shift = shift
            best_scale = int_scale

    return best_scale, best_shift


def rms_norm_f32(x, w):
    """RMS normalization in float32."""
    rms = np.sqrt(np.mean(x * x) + 1e-5)
    return (x / rms) * w


def rms_norm_int8_sim(x_int8, w_int8):
    """Simulate the C rms_norm_int8: integer arithmetic, return int8."""
    x = x_int8.astype(np.int32)
    w = w_int8.astype(np.int32)
    sum_sq = np.sum(x * x)
    mean_sq = sum_sq // len(x)
    if mean_sq == 0:
        mean_sq = 1
    rms = int(math.isqrt(int(mean_sq)))
    if rms == 0:
        rms = 1
    val = (x * w) // rms
    return np.clip(val, -128, 127).astype(np.int8)


def calibrate_with_simulation(config, tensors, quantized_tensors, tensor_scales):
    """
    Run a simulated forward pass to measure actual INT32 matmul output ranges
    per linear layer, then derive OUT_CVT params from real statistics.
    """
    n_layers = config['num_hidden_layers']
    hidden = config['hidden_size']
    n_heads = config['num_attention_heads']
    n_kv_heads = config['num_key_value_heads']
    di = config['intermediate_size']
    dqkv = hidden // n_heads
    kv_dim = n_kv_heads * dqkv

    # Simulate with a few calibration tokens
    calib_tokens = [1, 80, 147, 201, 282, 57, 100, 500, 1000, 42]

    # Collect max |INT32 output| per (tensor_name)
    int32_max = {}

    def matmul_int32_and_record(x_int8, w_name):
        """Do INT8 matmul in INT32, record max absolute value, return INT32 result."""
        w_int8 = quantized_tensors[w_name]
        # x_int8: (1, K),  w_int8: (N, K)  -> result (1, N)
        x = x_int8.astype(np.int32).flatten()
        w = w_int8.astype(np.int32)
        result = w @ x  # (N,) in INT32
        cur_max = int(np.max(np.abs(result)))
        if w_name not in int32_max or cur_max > int32_max[w_name]:
            int32_max[w_name] = cur_max
        return result

    tie_embed = config.get('tie_word_embeddings', False)
    embed_name = 'lm_head.weight' if tie_embed else 'model.embed_tokens.weight'
    embed_q = quantized_tensors[embed_name]

    for token_id in calib_tokens:
        if token_id >= config['vocab_size']:
            continue

        # Embedding lookup
        residual = embed_q[token_id].copy().astype(np.int8)

        for layer in range(n_layers):
            # RMS norm (attention)
            rms_w_name = f'model.layers.{layer}.input_layernorm.weight'
            rms_w = quantized_tensors[rms_w_name]
            normed = rms_norm_int8_sim(residual, rms_w)

            # Q/K/V projections — record INT32 ranges
            wq_name = f'model.layers.{layer}.self_attn.q_proj.weight'
            wk_name = f'model.layers.{layer}.self_attn.k_proj.weight'
            wv_name = f'model.layers.{layer}.self_attn.v_proj.weight'

            q_int32 = matmul_int32_and_record(normed, wq_name)
            k_int32 = matmul_int32_and_record(normed, wk_name)
            v_int32 = matmul_int32_and_record(normed, wv_name)

            # Simulate OUT_CVT to get INT8 attention input
            # (use preliminary scale for intermediate computation)
            def to_int8_prelim(int32_vec, max_val):
                if max_val == 0:
                    return np.zeros_like(int32_vec, dtype=np.int8)
                mult = 127.0 / max_val
                return np.clip(np.round(int32_vec * mult), -128, 127).astype(np.int8)

            # Skip full attention sim — just project O to get residual contribution
            attn_out = to_int8_prelim(q_int32[:hidden], max(1, int(np.max(np.abs(q_int32)))))

            wo_name = f'model.layers.{layer}.self_attn.o_proj.weight'
            o_int32 = matmul_int32_and_record(attn_out, wo_name)
            o_int8 = to_int8_prelim(o_int32, max(1, int(np.max(np.abs(o_int32)))))

            # Residual add (saturating)
            res32 = residual.astype(np.int32) + o_int8.astype(np.int32)
            residual = np.clip(res32, -128, 127).astype(np.int8)

            # RMS norm (FFN)
            rms_ffn_name = f'model.layers.{layer}.post_attention_layernorm.weight'
            rms_ffn_w = quantized_tensors[rms_ffn_name]
            normed2 = rms_norm_int8_sim(residual, rms_ffn_w)

            # MLP projections
            gate_name = f'model.layers.{layer}.mlp.gate_proj.weight'
            up_name = f'model.layers.{layer}.mlp.up_proj.weight'

            gate_int32 = matmul_int32_and_record(normed2, gate_name)
            up_int32 = matmul_int32_and_record(normed2, up_name)

            # SwiGLU approx for calibration
            gate_i8 = to_int8_prelim(gate_int32, max(1, int(np.max(np.abs(gate_int32)))))
            up_i8 = to_int8_prelim(up_int32, max(1, int(np.max(np.abs(up_int32)))))
            act = np.clip((gate_i8.astype(np.int32) * up_i8.astype(np.int32)) >> 7, -128, 127).astype(np.int8)

            down_name = f'model.layers.{layer}.mlp.down_proj.weight'
            down_int32 = matmul_int32_and_record(act, down_name)
            down_i8 = to_int8_prelim(down_int32, max(1, int(np.max(np.abs(down_int32)))))

            res32 = residual.astype(np.int32) + down_i8.astype(np.int32)
            residual = np.clip(res32, -128, 127).astype(np.int8)

        # Final LM head
        rms_out_w = quantized_tensors['model.norm.weight']
        normed_final = rms_norm_int8_sim(residual, rms_out_w)
        lm_int32 = matmul_int32_and_record(normed_final, 'lm_head.weight')

    print("\nCalibration INT32 max per linear layer:")
    outcvt_params = {}
    for name, max_val in int32_max.items():
        # OUT_CVT: output = saturate_int8(value * scale >> shift)
        # We want: max_val * scale >> shift = 127
        # So: multiplier = 127.0 / max_val
        if max_val == 0:
            max_val = 1
        # Use 90th percentile headroom: target 100 instead of 127
        # to leave room for values slightly above calibration max
        multiplier = 100.0 / max_val
        scale, shift = find_scale_shift(multiplier)
        outcvt_params[name] = (0, scale, shift)
        actual = scale / (1 << shift)
        print(f"  {name}: max_int32={max_val}, mult={multiplier:.6f}, "
              f"scale={scale}, shift={shift}, actual={actual:.6f}")

    return outcvt_params


def load_safetensors(path):
    """Load safetensors file, return dict of name -> numpy array."""
    with open(path, 'rb') as f:
        header_len = struct.unpack('<Q', f.read(8))[0]
        header_json = f.read(header_len).decode('utf-8')
        header = json.loads(header_json)
        data_start = 8 + header_len

        tensors = {}
        for name, info in header.items():
            if name == '__metadata__':
                continue
            dtype_str = info['dtype']
            shape = info['shape']
            offsets = info['data_offsets']

            f.seek(data_start + offsets[0])
            raw = f.read(offsets[1] - offsets[0])

            if dtype_str == 'F32':
                arr = np.frombuffer(raw, dtype=np.float32).reshape(shape)
            elif dtype_str == 'F16':
                arr = np.frombuffer(raw, dtype=np.float16).reshape(shape).astype(np.float32)
            elif dtype_str == 'BF16':
                raw16 = np.frombuffer(raw, dtype=np.uint16)
                raw32 = (raw16.astype(np.uint32) << 16)
                arr = raw32.view(np.float32).reshape(shape)
            else:
                raise ValueError(f"Unsupported dtype: {dtype_str}")

            tensors[name] = arr

    return tensors


def generate_rope_tables(max_pos, dqkv, rope_theta):
    """Generate INT8 cos/sin tables for RoPE."""
    half_d = dqkv // 2
    cos_table = np.zeros((max_pos, half_d), dtype=np.int8)
    sin_table = np.zeros((max_pos, half_d), dtype=np.int8)

    for pos in range(max_pos):
        for i in range(half_d):
            freq = 1.0 / (rope_theta ** (2.0 * i / dqkv))
            angle = pos * freq
            cos_table[pos, i] = np.clip(np.round(math.cos(angle) * 127), -128, 127).astype(np.int8)
            sin_table[pos, i] = np.clip(np.round(math.sin(angle) * 127), -128, 127).astype(np.int8)

    return cos_table, sin_table


def build_tlm(config, tensors, output_path):
    """Build .tlm model file."""
    n_layers = config['num_hidden_layers']
    hidden = config['hidden_size']
    n_heads = config['num_attention_heads']
    dqkv = hidden // n_heads
    max_pos = config['max_position_embeddings']
    rope_theta = config.get('rope_theta', 10000.0)
    tie_embed = config.get('tie_word_embeddings', False)

    # Collect all tensors in order with their IDs
    tensor_entries = []  # (tid, layer_idx, name, shape)

    # Embedding
    embed_name = 'lm_head.weight' if tie_embed else 'model.embed_tokens.weight'
    tensor_entries.append((TID_EMBEDDING, 0, embed_name, tensors[embed_name].shape))

    # Per-layer tensors
    layer_tensor_map = [
        (TID_RMS_ATT_W, 'model.layers.{}.input_layernorm.weight'),
        (TID_WQ,         'model.layers.{}.self_attn.q_proj.weight'),
        (TID_WK,         'model.layers.{}.self_attn.k_proj.weight'),
        (TID_WV,         'model.layers.{}.self_attn.v_proj.weight'),
        (TID_WO,         'model.layers.{}.self_attn.o_proj.weight'),
        (TID_RMS_FFN_W,  'model.layers.{}.post_attention_layernorm.weight'),
        (TID_W_GATE,     'model.layers.{}.mlp.gate_proj.weight'),
        (TID_W_UP,       'model.layers.{}.mlp.up_proj.weight'),
        (TID_W_DOWN,     'model.layers.{}.mlp.down_proj.weight'),
    ]

    for layer in range(n_layers):
        for tid, name_fmt in layer_tensor_map:
            name = name_fmt.format(layer)
            tensor_entries.append((tid, layer, name, tensors[name].shape))

    # Output norm
    tensor_entries.append((TID_RMS_OUT_W, 0, 'model.norm.weight', tensors['model.norm.weight'].shape))

    # LM head
    tensor_entries.append((TID_LM_HEAD, 0, 'lm_head.weight', tensors['lm_head.weight'].shape))

    # Quantize all tensors and collect scales
    quantized_tensors = {}
    tensor_scales = {}
    for tid, layer_idx, name, shape in tensor_entries:
        if name not in quantized_tensors:
            q, s = quantize_symmetric(tensors[name])
            quantized_tensors[name] = q
            tensor_scales[name] = s

    # Calibrate OUT_CVT via simulation
    #   (removed — scaled-int8 architecture uses dynamic per-op requant at
    #    runtime, only per-weight quant scale is stored)

    # Generate RoPE tables
    cos_table, sin_table = generate_rope_tables(max_pos, dqkv, rope_theta)

    # Build the binary
    # Header: magic(4) + config(10*4=40) + num_tensors(4) = 48 bytes
    num_tensors = len(tensor_entries)
    header = struct.pack('<4s', b'TLM1')
    header += struct.pack('<10I',
        hidden,
        config['intermediate_size'],
        n_heads,
        n_layers,
        config['num_key_value_heads'],
        config['vocab_size'],
        max_pos,
        config.get('bos_token_id', 1),
        config.get('eos_token_id', 2),
        1 if tie_embed else 0,
    )
    header += struct.pack('<I', num_tensors)

    # Per-tensor entries: 28 bytes each
    # Compute data offsets
    entry_table_size = num_tensors * 28
    rope_tables_size = max_pos * (dqkv // 2) * 2  # cos + sin
    data_base = len(header) + entry_table_size + rope_tables_size

    entries_bin = b''
    data_bin = b''
    current_offset = data_base

    for tid, layer_idx, name, shape in tensor_entries:
        q_data = quantized_tensors[name]
        flat = q_data.flatten().tobytes()
        rows = shape[0] if len(shape) >= 1 else 1
        cols = shape[1] if len(shape) >= 2 else 1

        # weight quant scale as Q20 fixed point (real = int8 * scale / 2^20)
        w_scale_q20 = max(1, int(round(tensor_scales[name] * (1 << 20))))

        entry = struct.pack('<HH II II II',
            tid, layer_idx,
            rows, cols,
            current_offset, len(flat),
            w_scale_q20, 0,
        )
        entries_bin += entry
        data_bin += flat
        current_offset += len(flat)

    # Assemble
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(entries_bin)
        f.write(cos_table.tobytes())
        f.write(sin_table.tobytes())
        f.write(data_bin)

    print(f"\nWritten {output_path}: {current_offset} bytes total")
    print(f"  {num_tensors} tensors, RoPE tables: {rope_tables_size} bytes")

    return tensor_scales


def build_tkn(tokenizer_json, output_path):
    """Build .tkn tokenizer binary."""
    with open(tokenizer_json, 'r', encoding='utf-8') as f:
        tok = json.load(f)

    model = tok['model']
    vocab = model['vocab']
    merges = model.get('merges', [])

    # Build vocab list sorted by token ID
    vocab_list = sorted(vocab.items(), key=lambda x: x[1])
    vocab_size = len(vocab_list)
    num_merges = len(merges)

    # Find special tokens
    bos_id = 1
    eos_id = 2
    for st in tok.get('added_tokens', []):
        if st.get('content') == '<|start_story|>':
            bos_id = st['id']
        elif st.get('content') == '<|end_story|>':
            eos_id = st['id']

    # Header
    header = struct.pack('<4s 4I', b'TKN1', vocab_size, num_merges, bos_id, eos_id)

    # Vocab entries: uint16 len + bytes
    vocab_bin = b''
    for token_str, token_id in vocab_list:
        token_bytes = token_str.encode('utf-8')
        vocab_bin += struct.pack('<H', len(token_bytes))
        vocab_bin += token_bytes

    # Merge entries: uint16 a, b, result
    # Parse "token_a token_b" format and find result in vocab
    merge_bin = b''
    for merge_str in merges:
        parts = merge_str.split(' ')
        if len(parts) != 2:
            continue
        a_str, b_str = parts
        merged = a_str + b_str
        a_id = vocab.get(a_str, 0)
        b_id = vocab.get(b_str, 0)
        result_id = vocab.get(merged, 0)
        merge_bin += struct.pack('<HHH', a_id, b_id, result_id)

    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(vocab_bin)
        f.write(merge_bin)

    print(f"Written {output_path}: {len(header) + len(vocab_bin) + len(merge_bin)} bytes")
    print(f"  vocab_size={vocab_size}, merges={num_merges}, bos={bos_id}, eos={eos_id}")


def main():
    parser = argparse.ArgumentParser(description='Quantize LLaMA model for TestOS')
    parser.add_argument('--model_dir', required=True, help='Path to model directory (with config.json, tokenizer.json, model.safetensors)')
    parser.add_argument('--output_dir', default='.', help='Output directory')
    parser.add_argument('--name', default='story', help='Output file base name')
    args = parser.parse_args()

    model_dir = Path(args.model_dir)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # Load config
    with open(model_dir / 'config.json', 'r') as f:
        config = json.load(f)
    print(f"Model config: hidden={config['hidden_size']}, layers={config['num_hidden_layers']}, "
          f"vocab={config['vocab_size']}, heads={config['num_attention_heads']}")

    # Load safetensors
    st_path = model_dir / 'model.safetensors'
    if not st_path.exists():
        candidates = list(model_dir.glob('*.safetensors'))
        if candidates:
            st_path = candidates[0]
        else:
            raise FileNotFoundError(f"No .safetensors file found in {model_dir}")

    print(f"Loading {st_path}...")
    tensors = load_safetensors(st_path)
    print(f"  Loaded {len(tensors)} tensors")

    # Build .tlm
    tlm_path = output_dir / f'{args.name}.tlm'
    scales = build_tlm(config, tensors, tlm_path)

    # Build .tkn
    tkn_path = output_dir / f'{args.name}.tkn'
    tokenizer_path = model_dir / 'tokenizer.json'
    if tokenizer_path.exists():
        build_tkn(tokenizer_path, tkn_path)
    else:
        print(f"Warning: {tokenizer_path} not found, skipping .tkn generation")

    print("\nDone! Files ready for TFTP loading.")


if __name__ == '__main__':
    main()
