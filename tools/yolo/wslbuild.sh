#!/bin/bash
export PATH="/home/ajax/SoftWare/compiler/aarch64-linux-musl-cross/bin:/usr/bin:/bin"
cd /mnt/c/Users/ajax/Desktop/ida/testos-reflector
make "$@" 2>&1 | tail -50
