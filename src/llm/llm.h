#ifndef LLM_H
#define LLM_H

#include "t_types.h"

/* ── .tlm file format ── */

#define TLM_MAGIC 0x314D4C54  /* "TLM1" */
#define TKN_MAGIC 0x314E4B54  /* "TKN1" */

/* Tensor IDs */
#define TID_EMBEDDING   0
#define TID_RMS_ATT_W   1
#define TID_WQ          2
#define TID_WK          3
#define TID_WV          4
#define TID_WO          5
#define TID_RMS_FFN_W   6
#define TID_W_GATE      7
#define TID_W_UP        8
#define TID_W_DOWN      9
#define TID_RMS_OUT_W   10
#define TID_LM_HEAD     11

typedef struct {
    uint32_t hidden_size;
    uint32_t intermediate_size;
    uint32_t num_attention_heads;
    uint32_t num_hidden_layers;
    uint32_t num_kv_heads;
    uint32_t vocab_size;
    uint32_t max_seq_len;
    uint32_t bos_token_id;
    uint32_t eos_token_id;
    uint32_t tie_word_embeddings;
} llm_config_t;

typedef struct {
    uint16_t tensor_id;
    uint16_t layer_idx;
    uint32_t rows;
    uint32_t cols;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t w_scale_q20;   /* weight quant scale, Q20 fixed (real = int8*scale/2^20) */
    uint32_t reserved;
} llm_tensor_entry_t;

/* ── Runtime model ── */

typedef struct {
    const int8_t *data;
    uint32_t      rows;
    uint32_t      cols;
    uint32_t      w_scale_q20;  /* weight quant scale in Q20 */
} llm_weight_t;

typedef struct {
    llm_config_t cfg;
    uint32_t     dqkv;       /* hidden / n_heads */
    uint32_t     n_groups;   /* n_q_h / n_kv_h */

    /* weights */
    llm_weight_t embedding;
    llm_weight_t rms_att_w[8];
    llm_weight_t wq[8];
    llm_weight_t wk[8];
    llm_weight_t wv[8];
    llm_weight_t wo[8];
    llm_weight_t rms_ffn_w[8];
    llm_weight_t w_gate[8];
    llm_weight_t w_up[8];
    llm_weight_t w_down[8];
    llm_weight_t rms_out_w;
    llm_weight_t lm_head;

    /* RoPE tables */
    const int8_t *cos_table;
    const int8_t *sin_table;

    /* KV cache: int8_t[n_layers][max_seq][n_kv_h * dqkv] + per-(layer,pos) scale */
    int8_t *k_cache;
    int8_t *v_cache;
    uint32_t *k_scale;    /* [n_layers * max_seq] Q20 */
    uint32_t *v_scale;    /* [n_layers * max_seq] Q20 */
    uint32_t kv_stride;   /* max_seq * n_kv_h * dqkv */

    /* work buffers */
    int8_t *residual;     /* [hidden] */
    int8_t *hidden;       /* [hidden] */
    int8_t *q_buf;        /* [n_q_h * dqkv] */
    int8_t *k_buf;        /* [n_kv_h * dqkv] */
    int8_t *v_buf;        /* [n_kv_h * dqkv] */
    int8_t *attn_out;     /* [n_q_h * dqkv] */
    int8_t *gate_buf;     /* [intermediate] */
    int8_t *up_buf;       /* [intermediate] */
    int8_t *sig_buf;      /* [intermediate] */
    int8_t *mlp_out;      /* [hidden] */
    int8_t *logits;       /* [vocab] */
    int32_t *logits32;    /* [vocab] raw INT32 logits for sampling */
    int8_t *attn_scores;  /* [max_seq] per head */

    /* repetition-penalty history (ring of recent output tokens) */
    uint32_t recent[16];
    uint32_t recent_n;

    /* NPU DMA buffers */
    int8_t  *dma_input;
    int8_t  *dma_weights;
    int8_t  *dma_output;
    uint64_t *dma_regs;
    void    *dma_regcmd;
    void    *dma_tasks;
} llm_model_t;

/* ── Tokenizer ── */

typedef struct {
    const uint8_t *base;
    uint32_t vocab_size;
    uint32_t num_merges;
    uint32_t bos_id;
    uint32_t eos_id;
    /* offsets parsed at init */
    const uint8_t *vocab_data;
    const uint8_t *merge_data;
    /* vocab offset table (built at init) */
    uint32_t *vocab_offsets;
    uint16_t *vocab_lens;
} llm_tokenizer_t;

/* ── API ── */

int  llm_model_load(llm_model_t *m, const void *tlm_data, uint32_t tlm_size);
int  llm_tokenizer_init(llm_tokenizer_t *tok, const void *tkn_data, uint32_t tkn_size);
int  llm_tokenizer_encode(llm_tokenizer_t *tok, const char *text, uint32_t *ids, int max_ids);
void llm_tokenizer_decode(llm_tokenizer_t *tok, uint32_t id, char *buf, int buflen);

uint32_t llm_forward(llm_model_t *m, uint32_t token_id, uint32_t pos);
void     llm_generate(llm_model_t *m, llm_tokenizer_t *tok,
                      const uint32_t *prompt_ids, int prompt_len, int max_tokens);

/* ── Operators ── */

void     llm_gather_int8(int8_t *out, const int8_t *table, uint32_t idx, int dim);
uint32_t llm_rms_norm_int8(int8_t *y, const int8_t *x, const int8_t *w,
                           uint32_t w_scale, int n);
void     llm_rope_int8(int8_t *data, int n_heads, int dqkv, int pos,
                       const int8_t *cos_table, const int8_t *sin_table, int half_d);
uint32_t llm_argmax(const int8_t *data, int n);

/* NPU matmul: INT8 in -> INT32 accumulate -> CPU requant to INT8 + scale.
 * output: int8 values [M*N]. Returns output scale in Q20 fixed point
 * (real = int8 * scale / 2^SCALE_SHIFT). in_scale is the input's Q20 scale. */
#define LLM_SCALE_SHIFT 20

uint32_t llm_npu_matmul(llm_model_t *m, int8_t *output,
                        const int8_t *input, int M, int K,
                        const llm_weight_t *weight, uint32_t in_scale,
                        int32_t *logits32_out);

#endif /* LLM_H */
