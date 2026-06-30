fatload mmc 1:1  0x300000 rk3588-orangepi-5-plus.dtb
fatload mmc 1:1  0x400000 kernel.uimg
bootm 0x400000 - 0x300000
