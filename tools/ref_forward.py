#!/usr/bin/env python3
"""
Float reference LLaMA forward — verifies the model produces a coherent story,
and serves as ground truth to compare the INT8 pipeline against layer by layer.
"""
import argparse, json, math
import numpy as np
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).parent))
from quantize_model import load_safetensors


def rms_norm(x, w, eps=1e-5):
    return x / np.sqrt(np.mean(x*x) + eps) * w


def softmax(x):
    x = x - x.max()
    e = np.exp(x)
    return e / e.sum()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--model_dir', required=True)
    ap.add_argument('--n', type=int, default=40)
    args = ap.parse_args()

    md = Path(args.model_dir)
    cfg = json.load(open(md/'config.json'))
    hidden = cfg['hidden_size']; n_layers = cfg['num_hidden_layers']
    n_heads = cfg['num_attention_heads']; n_kv = cfg['num_key_value_heads']
    di = cfg['intermediate_size']; vocab = cfg['vocab_size']
    max_pos = cfg['max_position_embeddings']; rope_theta = cfg.get('rope_theta', 10000.0)
    dqkv = hidden // n_heads; half = dqkv//2; n_groups = n_heads//n_kv
    tie = cfg.get('tie_word_embeddings', False)

    T = load_safetensors(list(md.glob('*.safetensors'))[0])
    embed = T['lm_head.weight' if tie else 'model.embed_tokens.weight']

    # rope freqs
    inv_freq = 1.0/(rope_theta ** (np.arange(0, dqkv, 2)/dqkv))

    def L(l, s): return f'model.layers.{l}.{s}.weight'

    def apply_rope(vec, n_h, pos):
        out = vec.copy().reshape(n_h, dqkv)
        for i in range(half):
            c = math.cos(pos*inv_freq[i]); s = math.sin(pos*inv_freq[i])
            a = out[:, i].copy(); b = out[:, i+half].copy()
            out[:, i] = a*c - b*s
            out[:, i+half] = b*c + a*s
        return out.reshape(-1)

    kc = [[None]*max_pos for _ in range(n_layers)]
    vc = [[None]*max_pos for _ in range(n_layers)]

    def forward(tid, pos):
        x = embed[tid].astype(np.float64).copy()
        for l in range(n_layers):
            h = rms_norm(x, T[L(l,'input_layernorm')])
            q = T[L(l,'self_attn.q_proj')] @ h
            k = T[L(l,'self_attn.k_proj')] @ h
            v = T[L(l,'self_attn.v_proj')] @ h
            q = apply_rope(q, n_heads, pos); k = apply_rope(k, n_kv, pos)
            kc[l][pos] = k; vc[l][pos] = v
            ao = np.zeros(n_heads*dqkv)
            for qh in range(n_heads):
                kvg = qh//n_groups
                qh_v = q[qh*dqkv:(qh+1)*dqkv]
                sc = np.array([qh_v @ kc[l][p][kvg*dqkv:(kvg+1)*dqkv] for p in range(pos+1)])/math.sqrt(dqkv)
                pr = softmax(sc)
                oh = sum(pr[p]*vc[l][p][kvg*dqkv:(kvg+1)*dqkv] for p in range(pos+1))
                ao[qh*dqkv:(qh+1)*dqkv] = oh
            x = x + T[L(l,'self_attn.o_proj')] @ ao
            h2 = rms_norm(x, T[L(l,'post_attention_layernorm')])
            g = T[L(l,'mlp.gate_proj')] @ h2
            u = T[L(l,'mlp.up_proj')] @ h2
            silu = g/(1.0+np.exp(-g))
            x = x + T[L(l,'mlp.down_proj')] @ (silu*u)
        h = rms_norm(x, T['model.norm.weight'])
        return T['lm_head.weight'] @ h

    tokj = json.load(open(md/'tokenizer.json', encoding='utf-8'))
    id2tok = {v:k for k,v in tokj['model']['vocab'].items()}

    prompt_ids = [80,147,201,282,57]
    pos = 0; nxt = 0
    for tid in prompt_ids:
        nxt = int(np.argmax(forward(tid, pos))); pos += 1
    toks = []
    for _ in range(args.n):
        toks.append(id2tok.get(nxt,'<unk>').replace('▁',' '))
        nxt = int(np.argmax(forward(nxt, pos))); pos += 1
    print("FLOAT reference generation:")
    print(''.join(toks))


if __name__ == '__main__':
    main()
