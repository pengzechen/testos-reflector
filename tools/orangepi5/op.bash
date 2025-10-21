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