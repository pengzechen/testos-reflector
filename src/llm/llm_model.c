#include "llm/llm.h"
#include "lib/t_string.h"
#include "lib/t_logger.h"
#include "mem/t_mem.h"

int
llm_model_load(llm_model_t *m, const void *tlm_data, uint32_t tlm_size)
{
    const uint8_t *base = (const uint8_t *)tlm_data;
    uint32_t pos = 0;

    /* magic */
    uint32_t magic = *(const uint32_t *)(base + pos);
    pos += 4;
    if (magic != TLM_MAGIC) {
        logger_error("LLM: bad magic 0x%x (expected TLM1)\n", magic);
        return -1;
    }

    /* config: 10 x uint32 */
    memcpy(&m->cfg, base + pos, sizeof(llm_config_t));
    pos += sizeof(llm_config_t);

    m->dqkv = m->cfg.hidden_size / m->cfg.num_attention_heads;
    m->n_groups = m->cfg.num_attention_heads / m->cfg.num_kv_heads;

    logger_info("LLM: hidden=%d layers=%d vocab=%d heads=%d kv_heads=%d dqkv=%d\n",
                m->cfg.hidden_size, m->cfg.num_hidden_layers,
                m->cfg.vocab_size, m->cfg.num_attention_heads,
                m->cfg.num_kv_heads, m->dqkv);

    /* num_tensors */
    uint32_t num_tensors = *(const uint32_t *)(base + pos);
    pos += 4;

    /* read tensor entries */
    const llm_tensor_entry_t *entries = (const llm_tensor_entry_t *)(base + pos);
    pos += num_tensors * sizeof(llm_tensor_entry_t);

    /* RoPE tables follow the entry table */
    uint32_t half_d = m->dqkv / 2;
    uint32_t rope_size = m->cfg.max_seq_len * half_d;
    m->cos_table = (const int8_t *)(base + pos);
    pos += rope_size;
    m->sin_table = (const int8_t *)(base + pos);
    pos += rope_size;

    /* populate weight pointers */
    for (uint32_t i = 0; i < num_tensors; i++) {
        const llm_tensor_entry_t *e = &entries[i];
        llm_weight_t w;
        w.data = (const int8_t *)(base + e->data_offset);
        w.rows = e->rows;
        w.cols = e->cols;
        w.w_scale_q20 = e->w_scale_q20;
        w.dma_w = NULL;   /* set later by llm_prelayout_weight for matmul weights */

        uint32_t l = e->layer_idx;
        switch (e->tensor_id) {
        case TID_EMBEDDING:  m->embedding = w; break;
        case TID_RMS_ATT_W:  m->rms_att_w[l] = w; break;
        case TID_WQ:         m->wq[l] = w; break;
        case TID_WK:         m->wk[l] = w; break;
        case TID_WV:         m->wv[l] = w; break;
        case TID_WO:         m->wo[l] = w; break;
        case TID_RMS_FFN_W:  m->rms_ffn_w[l] = w; break;
        case TID_W_GATE:     m->w_gate[l] = w; break;
        case TID_W_UP:       m->w_up[l] = w; break;
        case TID_W_DOWN:     m->w_down[l] = w; break;
        case TID_RMS_OUT_W:  m->rms_out_w = w; break;
        case TID_LM_HEAD:    m->lm_head = w; break;
        }
    }

    /* allocate KV cache + per-position scales */
    uint32_t kv_dim = m->cfg.num_kv_heads * m->dqkv;
    m->kv_stride = m->cfg.max_seq_len * kv_dim;
    uint32_t kv_total = m->cfg.num_hidden_layers * m->kv_stride;
    m->k_cache = (int8_t *)t_mem_alloc(kv_total);
    m->v_cache = (int8_t *)t_mem_alloc(kv_total);
    memset(m->k_cache, 0, kv_total);
    memset(m->v_cache, 0, kv_total);
    uint32_t kv_scale_n = m->cfg.num_hidden_layers * m->cfg.max_seq_len;
    m->k_scale = (uint32_t *)t_mem_alloc(kv_scale_n * sizeof(uint32_t));
    m->v_scale = (uint32_t *)t_mem_alloc(kv_scale_n * sizeof(uint32_t));

    /* allocate work buffers */
    uint32_t h = m->cfg.hidden_size;
    uint32_t di = m->cfg.intermediate_size;
    uint32_t vocab = m->cfg.vocab_size;
    uint32_t n_q_h = m->cfg.num_attention_heads;

    m->residual    = (int8_t *)t_mem_alloc(h);
    m->hidden      = (int8_t *)t_mem_alloc(h);
    m->q_buf       = (int8_t *)t_mem_alloc(n_q_h * m->dqkv);
    m->k_buf       = (int8_t *)t_mem_alloc(kv_dim);
    m->v_buf       = (int8_t *)t_mem_alloc(kv_dim);
    m->attn_out    = (int8_t *)t_mem_alloc(n_q_h * m->dqkv);
    m->gate_buf    = (int8_t *)t_mem_alloc(di);
    m->up_buf      = (int8_t *)t_mem_alloc(di);
    m->sig_buf     = (int8_t *)t_mem_alloc(di);
    m->mlp_out     = (int8_t *)t_mem_alloc(h);
    m->logits      = (int8_t *)t_mem_alloc(vocab);
    m->logits32    = (int32_t *)t_mem_alloc(vocab * sizeof(int32_t));
    m->attn_scores = (int8_t *)t_mem_alloc(m->cfg.max_seq_len);
    m->recent_n    = 0;

    /* NPU DMA buffers — NC1HWC2 layout pads to 32×32 blocks for weights
     * and 16-element groups for features. Compute padded sizes. */
    uint32_t max_K = MAX(h, di);      /* max input dimension */
    uint32_t max_N = MAX(MAX(h, di), vocab);  /* max output dimension */
    /* weight_int8 layout: ceil(K/32)*32 * ceil(N/32)*32 */
    uint32_t wt_K_pad = ((max_K + 31) / 32) * 32;
    uint32_t wt_N_pad = ((max_N + 31) / 32) * 32;
    /* feature_data layout: ceil(C/16)*16 * H * W */
    uint32_t feat_pad = ((max_K + 15) / 16) * 16;
    /* INT32 output: C2=4, 4 bytes/elem. ceil(N/4)*4 elems * 4 bytes */
    uint32_t out_pad  = ((max_N + 3) / 4) * 4;

    m->dma_input   = (int8_t *)t_mem_alloc(feat_pad + 4096);
    m->dma_weights = (int8_t *)t_mem_alloc(wt_K_pad * wt_N_pad + 4096);
    m->dma_output  = (int8_t *)t_mem_alloc(out_pad * 4 + 4096);
    m->dma_regs    = (uint64_t *)t_mem_alloc(112 * sizeof(uint64_t));
    m->dma_regcmd  = t_mem_alloc(1024);
    m->dma_tasks   = t_mem_alloc(1024);

    logger_info("LLM: DMA bufs — input=%d weights=%d output=%d\n",
                feat_pad + 4096, wt_K_pad * wt_N_pad + 4096, out_pad + 4096);
    logger_info("LLM: DMA addrs — input=%p weights=%p output=%p regcmd=%p\n",
                m->dma_input, m->dma_weights, m->dma_output, m->dma_regcmd);

    logger_info("LLM: model base=%p end=%p\n", base, base + tlm_size);

    /* weight integrity check */
    logger_info("LLM: wq[0] data=%p first8: %d %d %d %d %d %d %d %d\n",
                m->wq[0].data,
                (signed char)m->wq[0].data[0], (signed char)m->wq[0].data[1],
                (signed char)m->wq[0].data[2], (signed char)m->wq[0].data[3],
                (signed char)m->wq[0].data[4], (signed char)m->wq[0].data[5],
                (signed char)m->wq[0].data[6], (signed char)m->wq[0].data[7]);

    logger_info("LLM: model loaded, KV cache %d bytes, work bufs ~%d bytes\n",
                kv_total * 2, h + di * 3 + vocab);

    /* Pre-lay-out all matmul weights into resident, cache-clean DMA buffers.
     * This removes per-token weight memset+layout+flush (LM head alone is
     * ~256 KB re-laid every token otherwise). */
    for (uint32_t l = 0; l < m->cfg.num_hidden_layers; l++) {
        llm_prelayout_weight(m, &m->wq[l]);
        llm_prelayout_weight(m, &m->wk[l]);
        llm_prelayout_weight(m, &m->wv[l]);
        llm_prelayout_weight(m, &m->wo[l]);
        llm_prelayout_weight(m, &m->w_gate[l]);
        llm_prelayout_weight(m, &m->w_up[l]);
        llm_prelayout_weight(m, &m->w_down[l]);
    }
    llm_prelayout_weight(m, &m->lm_head);
    logger_info("LLM: matmul weights pre-laid-out into resident DMA buffers\n");

    return 0;
}
