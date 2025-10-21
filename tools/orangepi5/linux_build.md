

git clone https://github.com/orangepi-xunlong/orangepi-build.git -n next

docker run -ti --name orangepi-build --privileged=true --cap-add=ALL -v /dev:/dev -v .:/orangepi-build/ ubuntu:22.04 bash

sed \
-e 's|archive.ubuntu.com|mirrors.tuna.tsinghua.edu.cn|g' \
-e 's|security.ubuntu.com|mirrors.tuna.tsinghua.edu.cn|g' \
-i.bak \
/etc/apt/sources.list
apt update


apt install -y systemd whiptail sudo locales git fdisk



