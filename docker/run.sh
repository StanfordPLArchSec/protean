#!/bin/bash

name=
script_path=${BASH_SOURCE[0]}
script_dir=$(realpath $(dirname ${script_path}))
root=$(dirname ${script_dir})
ptex_volume=ptex-volume

usage() {
    cat <<EOF
usage: $0 [name=ptex]
EOF
}

case $# in
    0)
	name=ptex
	;;
    1)
	name="$1"
	;;
    *)
	usage >&2
	exit 1
	;;
esac

docker volume create --driver local --opt type=nfs --opt o=addr=10.79.12.154,rw --opt "device=:$root" "$ptex_volume"
docker run -it --device /dev/kvm --cap-add=SYS_ADMIN --cap-add=NET_ADMIN \
       --mount source="$ptex_volume",target=$root \
       --privileged \
       "$name"
