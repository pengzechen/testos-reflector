bootdev hunt ethernet

setenv netmask 255.255.255.0
setenv gatewayip 192.168.1.1
setenv ipaddr 192.168.1.60
setenv serverip 192.168.1.8

tftp 0x400000 kernel.uimg
tftp 0x300000 rk3588-orangepi-5-plus.dtb

tftp  0x7F000000 elf_table.bin

# Load ELF files
# NOTICE: libstdc++.so.6.0.29 larger than 16Mb
tftp  0x80000000 libc.so
tftp  0x81000000 libgcc_s.so.1
tftp  0x82000000 libstdc++.so.6.0.29 
tftp  0x84000000 libitm.so.1.0.0
tftp  0x85000000 hello.elf
tftp  0x86000000 simple.elf
tftp  0x87000000 hello_cpp.elf

bootm 0x400000 - 0x300000

