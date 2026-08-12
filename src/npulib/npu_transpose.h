#ifndef NPU_TRANSPOSE_H
#define NPU_TRANSPOSE_H

#include "t_types.h"

/*
 * Transpose primitive for the RK3588 NPU.
 * ---------------------------------------------------------------------------
 * Reverse-engineered from librknnrt.so (IDA: librknnrt.so.i64).
 *
 * WHAT THE HARDWARE ACTUALLY DOES
 *   The NPU has NO dedicated transpose datapath in the shared regcmd path.
 *   Transpose rides the SAME "ABC" GEMM engine as Conv / MatMul / Pool, but
 *   configured as a pure feature load->store COPY (no weights, no MAC):
 *     - RKNPUEmitter_emitABC_T_BAC_regtask   @0x5a7200  (top-level)
 *         * fast/contiguous path (0x5a7858) -> emit_abc_regtask_matmul_v4 @0x59b3b0
 *           called with src_off == dst_off  (plain contiguous copy)
 *         * general tile path   (0x5a755c) -> emit_abc_regtask_tile       @0x59ab78
 *           called with the AXIS SWAP baked into the src/dst DMA addresses.
 *
 *   HW supports EXACTLY perm [0,2,1,3] on a 4D tensor -- i.e. swap axes 1<->2
 *   (validation strings: "emitABC_T_BAC_regtask tensor must be 4D",
 *    "C must be aligned to subc").
 *
 *   The swap is entirely in the base addresses (C2 = subchannel width, the
 *   contiguous innermost group):
 *     src = C2*i3 + batch_base + blk1*outer1_idx*C2 + outer1*blk0_idx*blk1*C2
 *     dst = C2*i3 + batch_base + blk0*outer1_idx*blk1*C2 + blk1*blk0_idx*C2
 *   The two MIDDLE-dim strides are swapped between read and write. That is the
 *   whole operation -- everything else is a straight copy.
 *
 * WHY THE NATIVE HW PATH IS NOT BIT-EXACT YET
 *   The register VALUES are produced by RKNPUExecutor_setup_register_base
 *   (@0x5973c8) reading the op descriptor, followed by ~30 emitter-vtable
 *   setters. The setters that would flip the engine into datamove/copy mode
 *   (MAC + weight-load bypass) are chip-class VIRTUAL slots -- on the analyzed
 *   class many resolve to ret-0 stubs (emitter_vtable_default_stub_ret0_a/b/c,
 *   emitter_dma_src_notch_addr_setter_stub @0x105020 = vtable+5496,
 *   emitter_dma_dst_base_addr_setter_stub @0x104810 = vtable+3432). The
 *   geometry is fully recovered; the copy-mode config bits are the one open
 *   piece and must be confirmed on real hardware.
 *
 * DELIVERABLE
 *   transpose_int8()       -- CORRECT, general 4D CPU transpose. Ships now,
 *                             golden reference (same CPU-op precedent as
 *                             reshape/softmax/sigmoid/mul).
 *   gen_transpose2d_int8() -- REAL on-NPU 2D transpose, lowered onto the
 *                             HW-VERIFIED matmul path (see below). This is the
 *                             one the on-board test runs, and it is bit-exact
 *                             by construction -- the same "lower onto a proven
 *                             datapath" strategy that made deconv real
 *                             (gen_deconv_int8 -> gen_conv2d_int8).
 *   gen_transpose_int8()   -- HW scaffold for the NATIVE ABC datamove/copy of
 *                             perm [0,2,1,3]. Still HW-VERIFY-PENDING: its
 *                             copy-mode config bits live behind chip-class
 *                             vtable setters (see note above) and could not be
 *                             confirmed statically. Kept for the RE record; NOT
 *                             the path the test exercises.
 *
 * TRANSPOSE AS AN IDENTITY-ACTIVATION MATMUL (gen_transpose2d_int8)
 *   The verified matmul computes  out[m][n] = sum_k A[m][k] * B[n][k]
 *   with A = the [M,K] activation (feature_data(K,M,1,16,...)), B = the [N,K]
 *   weight kernels (weight_int8(K,...)), output read at feature_data(N,M,1,4,).
 *   To transpose X[P,Q] -> Y[Q,P] (Y[a][b] = X[b][a]) pick
 *       B = X            (N=P kernels, each length K=Q)
 *       A = identity_Q   (M=K=Q,  A[m][k] = (m==k))
 *   then out[m][n] = sum_k (m==k) * X[n][k] = X[n][m] = Y[m][n].  A single
 *   non-zero term per output => the INT32 result equals the (sign-extended)
 *   INT8 element exactly, no MAC overflow. The engine literally runs a matmul
 *   (real MACs, real weight load) that HAPPENS to compute the transpose, so it
 *   inherits the matmul path's on-HW correctness with zero new register RE.
 *
 *   Alignment: the matmul path requires M(=Q)%4==0, K(=Q)%32==0, AND
 *   N(=P)%32==0. The N%32 rule is empirical/board-confirmed (a half-32 N such
 *   as 16 corrupts the descriptor geometry and hangs the NPU -- see the YOLO
 *   port note); test_matmul.c's N%16 check is an under-constraint. We pad both
 *   P and Q up to a multiple of 32 with zeros; the extra kernels/columns only
 *   produce discarded output rows/cols, so the valid [0,Q)x[0,P) region stays
 *   bit-exact (mirrors the deconv align-4 pad).
 * ---------------------------------------------------------------------------
 */

typedef struct
{
    /* Logical input shape (row-major NxHxWxC2-style 4D cube). */
    uint16_t dims[4];      /* d0, d1, d2, d3 */
    uint8_t  perm[4];      /* permutation; HW route requires {0,2,1,3} */
    uint8_t  elem_size;    /* bytes per element (1 for int8) */

    /* CPU path (transpose_int8). */
    const void *input;
    void       *output;

    /* HW path (gen_transpose_int8). */
    uint32_t input_dma;
    uint32_t output_dma;
    uint64_t *tasks;
    uint16_t num_tiles;
} transpose_params_t;

/* Correct general 4D transpose on the CPU (works today). */
void transpose_int8(transpose_params_t *p);

/*
 * Emit NPU tasks for a HW transpose of perm [0,2,1,3].
 * Returns 0 on success (params->num_tiles set), -1 for an unsupported
 * permutation or shape.  HW-VERIFY-PENDING: see header note.
 */
int gen_transpose_int8(transpose_params_t *p);

/* ------------------------------------------------------------------------- *
 * REAL on-NPU 2D transpose, lowered to an identity-activation matmul.
 * ------------------------------------------------------------------------- */

/* Aligned dims required by the matmul path. BOTH N(=P) and K(=Q) must be
 * multiples of 32: the INT8 datapath tiles weight kernels (N) and input
 * channels (K) in 32-groups, and a non-mult-32 N/K corrupts the descriptor
 * geometry and HANGS the NPU. This is board-confirmed in the YOLO port
 * (int_raw=0xc0000045) and was independently hit by the transpose's original
 * N=16 half-tile (int_raw=0xc00000aa, same 0xc0000000 fault class). A
 * multiple of 32 also satisfies the M%4 rule.
 *
 * NB: the N%16 check in test_matmul.c is an UNDER-constraint that only "worked"
 * because that test used N=32; the real silicon rule is N%32==0. */
static inline int
transpose2d_align_p(int p)
{
    return (p + 31) & ~31;
}
static inline int
transpose2d_align_q(int q)
{
    return (q + 31) & ~31;
}

typedef struct
{
    uint16_t p; /* rows of X  (= columns of Y)      */
    uint16_t q; /* columns of X (= rows of Y)       */

    /* DMA bases (caller lays out the buffers, sized with the aligned dims): */
    uint32_t input_dma;   /* identity activation, feature_data(Qa,Qa,1,16,k,m,1) */
    uint32_t weights_dma; /* X as N=Pa kernels of length K=Qa, weight_int8(Qa,n,k) */
    uint32_t output_dma;  /* INT32 output, read at feature_data(Pa,Qa,1,4,n,m,1) */

    uint64_t *tasks;      /* >= 112 uint64_t */
} transpose2d_params_t;

/*
 * Emit the NPU register block for a 2D transpose Y[Q,P] = X[P,Q]^T by lowering
 * it to an identity-activation matmul (see header note). Returns the
 * gen_matmul_int8 status (0 ok, -1 feature banks overflow, -2 weight too big).
 * Output is INT32 (the transposed INT8 values, sign-extended); bit-exact vs
 * transpose2d_int8_ref.
 */
int gen_transpose2d_int8(transpose2d_params_t *p);

/*
 * CPU golden for the 2D transpose: Y[Q][P], Y[a][b] = X[b][a]. X is the plain
 * row-major [P][Q] INT8 matrix; Y is row-major [Q][P] INT32.
 */
void transpose2d_int8_ref(const int8_t *X, int32_t *Y, int P, int Q);

#endif  // NPU_TRANSPOSE_H
