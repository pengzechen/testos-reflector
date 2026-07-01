#include "llm/llm.h"
#include "lib/t_string.h"
#include "lib/t_logger.h"
#include "mem/t_mem.h"

int
llm_tokenizer_init(llm_tokenizer_t *tok, const void *tkn_data, uint32_t tkn_size)
{
    (void)tkn_size;
    const uint8_t *base = (const uint8_t *)tkn_data;
    uint32_t pos = 0;

    uint32_t magic = *(const uint32_t *)(base + pos);
    pos += 4;
    if (magic != TKN_MAGIC) {
        logger_error("TKN: bad magic 0x%x\n", magic);
        return -1;
    }

    tok->base       = base;
    tok->vocab_size  = *(const uint32_t *)(base + pos); pos += 4;
    tok->num_merges  = *(const uint32_t *)(base + pos); pos += 4;
    tok->bos_id      = *(const uint32_t *)(base + pos); pos += 4;
    tok->eos_id      = *(const uint32_t *)(base + pos); pos += 4;

    tok->vocab_data = base + pos;

    /* build offset table for fast vocab lookup */
    tok->vocab_offsets = (uint32_t *)t_mem_alloc(tok->vocab_size * sizeof(uint32_t));
    tok->vocab_lens    = (uint16_t *)t_mem_alloc(tok->vocab_size * sizeof(uint16_t));

    const uint8_t *p = tok->vocab_data;
    for (uint32_t i = 0; i < tok->vocab_size; i++) {
        tok->vocab_offsets[i] = (uint32_t)(p - base);
        uint16_t len = *(const uint16_t *)p;
        tok->vocab_lens[i] = len;
        p += 2 + len;
    }

    tok->merge_data = p;

    logger_info("TKN: vocab=%d merges=%d bos=%d eos=%d\n",
                tok->vocab_size, tok->num_merges, tok->bos_id, tok->eos_id);
    return 0;
}

static const char *
tok_get_str(llm_tokenizer_t *tok, uint32_t id, uint16_t *out_len)
{
    if (id >= tok->vocab_size) {
        *out_len = 0;
        return "";
    }
    const uint8_t *p = tok->base + tok->vocab_offsets[id];
    uint16_t len = *(const uint16_t *)p;
    *out_len = len;
    return (const char *)(p + 2);
}

void
llm_tokenizer_decode(llm_tokenizer_t *tok, uint32_t id, char *buf, int buflen)
{
    uint16_t len;
    const char *s = tok_get_str(tok, id, &len);
    /* Replace the SentencePiece word-boundary marker "▁" (U+2581,
     * UTF-8 bytes E2 96 81) with a plain space. */
    int o = 0;
    for (int i = 0; i < len && o < buflen - 1; i++) {
        if (i + 2 < len &&
            (uint8_t)s[i] == 0xE2 && (uint8_t)s[i + 1] == 0x96 &&
            (uint8_t)s[i + 2] == 0x81) {
            buf[o++] = ' ';
            i += 2;   /* skip the 3-byte sequence */
        } else {
            buf[o++] = s[i];
        }
    }
    buf[o] = '\0';
}

static int
tok_match(llm_tokenizer_t *tok, uint32_t id, const char *str, int slen)
{
    uint16_t tlen;
    const char *ts = tok_get_str(tok, id, &tlen);
    if (tlen != (uint16_t)slen) return 0;
    for (int i = 0; i < slen; i++) {
        if (ts[i] != str[i]) return 0;
    }
    return 1;
}

static uint32_t
tok_find(llm_tokenizer_t *tok, const char *str, int slen)
{
    for (uint32_t i = 0; i < tok->vocab_size; i++) {
        if (tok_match(tok, i, str, slen))
            return i;
    }
    return 0; /* <unk> */
}

int
llm_tokenizer_encode(llm_tokenizer_t *tok, const char *text, uint32_t *ids, int max_ids)
{
    int text_len = 0;
    while (text[text_len]) text_len++;

    /* UTF-8 byte-level: start with one token per byte */
    int n = 0;

    /* prepend "▁" (U+2581, 3 bytes: E2 96 81) as normalizer does */
    {
        char buf[4] = {(char)0xE2, (char)0x96, (char)0x81, 0};
        uint32_t id = tok_find(tok, buf, 3);
        if (n < max_ids) ids[n++] = id;
    }

    /* encode each byte as a token */
    for (int i = 0; i < text_len && n < max_ids; i++) {
        char c = text[i];
        if (c == ' ') {
            /* space -> "▁" */
            char buf[4] = {(char)0xE2, (char)0x96, (char)0x81, 0};
            uint32_t id = tok_find(tok, buf, 3);
            if (n < max_ids) ids[n++] = id;
        } else {
            /* try single char as token, fall back to byte_fallback <0xNN> */
            uint32_t id = tok_find(tok, &c, 1);
            if (n < max_ids) ids[n++] = id;
        }
    }

    /* apply BPE merges greedily */
    const uint8_t *merge_p = tok->merge_data;
    for (uint32_t mi = 0; mi < tok->num_merges && n > 1; mi++) {
        uint16_t a_id = *(const uint16_t *)(merge_p);
        uint16_t b_id = *(const uint16_t *)(merge_p + 2);
        uint16_t r_id = *(const uint16_t *)(merge_p + 4);
        merge_p += 6;

        for (int i = 0; i < n - 1; i++) {
            if (ids[i] == a_id && ids[i + 1] == b_id) {
                ids[i] = r_id;
                /* shift remaining tokens left */
                for (int j = i + 1; j < n - 1; j++)
                    ids[j] = ids[j + 1];
                n--;
                i--; /* re-check at this position */
            }
        }
    }

    return n;
}
