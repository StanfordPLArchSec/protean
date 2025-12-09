#!/bin/bash

set -eu

cd "$(dirname "${BASH_SOURCE[0]}")"
docker save protean:latest -o protean.tar
