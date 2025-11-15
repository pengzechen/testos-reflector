
#ifndef _TCR_H
#define _TCR_H

// =====================  TCR 寄存器 ============================
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/* TCR_EL1 寄存器字段宏定义 */

/* 低地址空间（TTBR0_EL1）翻译控制 */
#define TCR_T0SZ(x)  ((x) & 0x3F)        /* 低地址空间的区域大小 (6 位) */
#define TCR_IRGN0(x) (((x) & 0x3) << 8)  /* 低地址空间的内部缓存属性 (2 位) */
#define TCR_ORGN0(x) (((x) & 0x3) << 10) /* 低地址空间的外部缓存属性 (2 位) */
#define TCR_SH0(x)   (((x) & 0x3) << 12) /* 低地址空间的共享属性 (2 位) */
#define TCR_TG0(x)   (((x) & 0x3) << 14) /* 低地址空间的翻译粒度 (2 位) */


/* 高地址空间（TTBR1_EL1）翻译控制 */
#define TCR_T1SZ(x)  (((x) & 0x3F) << 16) /* 高地址空间的区域大小 (6 位) */
#define TCR_IRGN1(x) (((x) & 0x3) << 24)  /* 高地址空间的内部缓存属性 (2 位) */
#define TCR_ORGN1(x) (((x) & 0x3) << 26)  /* 高地址空间的外部缓存属性 (2 位) */
#define TCR_SH1(x)   (((x) & 0x3) << 28)  /* 高地址空间的共享属性 (2 位) */
#define TCR_TG1(x)   (((x) & 0x3) << 30)  /* 高地址空间的翻译粒度 (2 位) */

// 1、 TCR_T0SZ 值：
// 48-bit VA (常见)   16
// 39-bit VA	     25
// 36-bit VA	     28

// 2、 TG0 值：
// 字段	值	页大小
// TG0 (bit 15:14)	00	4KB
// TG0	01	64KB
// TG0	10	16KB
// TG1 (bit 31:30)	10	4KB
// TG1	01	16KB
// TG1	11	64KB

// 3、SH0 值：
// 值	含义
// 00	Non-shareable
// 10	Outer shareable
// 11	Inner shareable  //多核共享一致

// 4、orgn0，irgn0
// 位段	             含义
// ORGN0 [11:10]	Outer Cache 属性
// IRGN0 [9:8]	    Inner Cache 属性
// 编码：
// 值	含义
// 00	Non-cacheable
// 01	Write-Back, Write-Allocate
// 10	Write-Through, no WA
// 11	Write-Back, no WA

/* 物理地址大小 (IPS) - TCR_EL1[34:32] */
#define TCR_IPS_32BITS  (0ULL << 32)  /* 32 bits, 4GB */
#define TCR_IPS_36BITS  (1ULL << 32)  /* 36 bits, 64GB */
#define TCR_IPS_40BITS  (2ULL << 32)  /* 40 bits, 1TB */
#define TCR_IPS_42BITS  (3ULL << 32)  /* 42 bits, 4TB */
#define TCR_IPS_44BITS  (4ULL << 32)  /* 44 bits, 16TB */
#define TCR_IPS_48BITS  (5ULL << 32)  /* 48 bits, 256TB */

/* TCR_EL1 完整配置 */
#define TCR_EL1 (TCR_T0SZ(64 - 48) |      /* 48位虚拟地址 */ \
                 TCR_T1SZ(64 - 48) |      /* 48位虚拟地址 */ \
                 TCR_TG0(0) |             /* 4KB 页 */ \
                 TCR_TG1(2) |             /* 4KB 页 */ \
                 TCR_SH0(3) |             /* Inner shareable */ \
                 TCR_IRGN0(1) |           /* Inner Write-Back Write-Allocate */ \
                 TCR_ORGN0(1) |           /* Outer Write-Back Write-Allocate */ \
                 TCR_IPS_48BITS)          /* 48位物理地址 */

#endif // _TCR_H