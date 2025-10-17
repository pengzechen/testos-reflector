ENTRY(_start)

SECTIONS
{
    /* 设置内核加载地址  */
    . = __LOAD_ADDR__;

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        __bss_end = .;
    }

    . = ALIGN((1 << 14));
    PROVIDE(__heap_flag = .);
}
