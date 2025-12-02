#!/bin/bash

script_dir="$(dirname "${BASH_SOURCE[0]}")"
cd "${script_dir}"

uid=$(id -u)
gid=20116 # cafe
KVM_GID=$(getent group kvm | cut -d':' -f3)

docker build --build-arg UID=$uid --build-arg GID=$gid --build-arg USERNAME=$(whoami) --build-arg KVM_GID=$KVM_GID -t ptex .
