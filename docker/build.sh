#!/bin/bash

set -e

script_dir="$(dirname "${BASH_SOURCE[0]}")"
cd "${script_dir}"

docker build --platform=linux/amd64 -t protean .
