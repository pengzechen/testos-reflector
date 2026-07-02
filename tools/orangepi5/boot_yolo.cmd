

bootdev hunt ethernet
setenv ipaddr 192.168.100.2
setenv serverip 192.168.100.1
setenv netmask 255.255.255.0
tftp  0x300000 testos-reflector/tools/orangepi5/rk3588-orangepi-5-plus.dtb
tftp  0x21000000 testos-reflector/tools/yolo/weights/yolov5n.ydev
tftp  0x400000 testos-reflector/kernel.uimg
bootm 0x400000 - 0x300000

bootdev hunt ethernet;setenv ipaddr 192.168.100.2;setenv serverip 192.168.100.1;setenv netmask 255.255.255.0;tftp 0x300000 testos-reflector/tools/orangepi5/rk3588-orangepi-5-plus.dtb;tftp 0x21000000 testos-reflector/tools/yolo/weights/yolov5n.ydev;tftp 0x400000 testos-reflector/kernel.uimg;bootm 0x400000 - 0x300000
