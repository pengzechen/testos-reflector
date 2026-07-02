#include "t_types.h"
#include "llm/llm.h"
#include "lib/t_logger.h"
#include "mem/t_mem.h"

/* TFTP load addresses — model and tokenizer loaded before kernel by U-Boot */
#define LLM_MODEL_ADDR  0x20000000   /* 512MB — above heap end */
#define LLM_TOKEN_ADDR  0x20200000   /* +2MB for tokenizer */
#define LLM_MODEL_SIZE  (2 * 1024 * 1024)   /* max 2MB */
#define LLM_TOKEN_SIZE  (256 * 1024)         /* max 256KB */

static llm_model_t     g_model;
static llm_tokenizer_t g_tokenizer;

void
rknpu_test_llm(void)
{
    logger_info("========================================\n");
    logger_info("  LLM INT8 Inference Test\n");
    logger_info("========================================\n");

    /* load model from TFTP pre-loaded memory */
    logger_info("Loading model from 0x%x...\n", LLM_MODEL_ADDR);
    int ret = llm_model_load(&g_model, (const void *)(uint64_t)LLM_MODEL_ADDR, LLM_MODEL_SIZE);
    if (ret != 0) {
        logger_error("Model load failed: %d\n", ret);
        return;
    }

    /* load tokenizer */
    logger_info("Loading tokenizer from 0x%x...\n", LLM_TOKEN_ADDR);
    ret = llm_tokenizer_init(&g_tokenizer, (const void *)(uint64_t)LLM_TOKEN_ADDR, LLM_TOKEN_SIZE);
    if (ret != 0) {
        logger_error("Tokenizer load failed: %d\n", ret);
        return;
    }

    /* generate several stories, each from a different opening prompt */
    static const char *prompts[] = {
        "Once upon a time",
        "One day a cat",
        "There was a little girl who",
    };
    int n_prompts = (int)(sizeof(prompts) / sizeof(prompts[0]));

    for (int s = 0; s < n_prompts; s++) {
        const char *prompt = prompts[s];
        uint32_t ids[64];
        int n_ids = llm_tokenizer_encode(&g_tokenizer, prompt, ids, 64);

        logger_info("\n--- Story %d/%d ---\n", s + 1, n_prompts);
        logger_info("Prompt: \"%s\" -> %d tokens: ", prompt, n_ids);
        for (int i = 0; i < n_ids; i++)
            logger_info("%d ", ids[i]);
        logger_info("\n");

        llm_generate(&g_model, &g_tokenizer, ids, n_ids, 256);
    }

    logger_info("========================================\n");
    logger_info("  LLM Test Complete\n");
    logger_info("========================================\n");
}
