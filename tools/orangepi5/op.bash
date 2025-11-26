sudo nmcli device wifi connect "CMCC-3wEw-5G" password "zppqz335"
git config --global http.https://github.com.proxy http://192.168.1.12:1080

sudo apt update
sudo apt install python3-pip -y
sudo apt install ninja-build -y
sudo apt install libdrm-dev -y
export PATH=$HOME/.local/bin:$PATH

pip3 install meson

git clone https://github.com/mtx512/rk3588-npu.git
cd rk3588-npu
meson setup build
ninja -C build

docker start -ai orangepi-build
cp output/images/Orangepi5plus_1.2.0_ubuntu_jammy_server_linux5.10.160/Orangepi5plus_1.2.0_ubuntu_jammy_server_linux5.10.160.img /orangepi-build
sudo rkdeveloptool db  /home/ajax/Projects/operating-system/testos-reflector-src/tools/orangepi5/MiniLoaderAll.bin
sudo rkdeveloptool wl 0 output/images/Orangepi5plus_1.2.0_ubuntu_jammy_server_linux5.10.160/Orangepi5plus_1.2.0_ubuntu_jammy_server_linux5.10.160.img


sudo scp ./output/debs/linux-image-legacy-rockchip-rk3588_1.2.0_arm64.deb  orangepi@192.168.1.28:/home/orangepi
sudo apt purge -y linux-image-legacy-rockchip-rk3588 && sudo dpkg -i linux-image-legacy-rockchip-rk3588_1.2.0_arm64.deb && sudo reboot 


# 烧文件系统
make test_app
sudo rkdeveloptool db  /home/ajax/Projects/operating-system/testos-reflector-src/tools/orangepi5/MiniLoaderAll.bin
sudo rkdeveloptool gpt /home/ajax/Projects/rk3588/StarryOS/crates/axplat-opi5p/tools/orangepi5/parameter.txt
sudo rkdeveloptool cs 2
sudo rkdeveloptool wlx root /home/ajax/Projects/rk3588/StarryOS/arceos/disk.img

make ARCH=aarch64 LOG=debug opi5p
make ARCH=aarch64 LOG=debug flash


aarch64-linux-musl-gcc -O2 -Wall -Wextra \
  -nostdlib -nostartfiles \
  -static -fPIE -pie \
  -e main \
  simple.c -o simple.elf


sudo minicom -b 1500000 -D /dev/ttyUSB0 --color=on

aarch64-linux-musl-g++ hello_cpp.cpp -Wl,--export-dynamic -ohello_cpp.elf 
aarch64-linux-musl-objdump -x -d -S hello_cpp.elf > ../c++.txt

sudo cp testos-reflector-src_aarch64-opi5p.uimg /data/docker/tftpboot/data/kernel.uimg
sudo cp tools/orangepi5/rk3588-orangepi-5-plus.dtb /data/docker/tftpboot/data/rk3588-orangepi-5-plus.dtb
sudo cp userapp/elf_table.bin /data/docker/tftpboot/data/elf_table.bin
sudo cp userapp/hello.elf /data/docker/tftpboot/data/hello.elf
sudo cp userapp/simple.elf /data/docker/tftpboot/data/simple.elf
sudo cp userapp/hello_cpp.elf /data/docker/tftpboot/data/hello_cpp.elf
sudo cp tools/musl-libs/lib/libc.so /data/docker/tftpboot/data/libc.so
sudo cp tools/musl-libs/lib/libgcc_s.so.1 /data/docker/tftpboot/data/libgcc_s.so.1
sudo cp tools/musl-libs/lib/libstdc++.so.6.0.29 /data/docker/tftpboot/data/libstdc++.so.6.0.29
sudo cp tools/musl-libs/lib/libitm.so.1.0.0 /data/docker/tftpboot/data/libitm.so.1.0.0