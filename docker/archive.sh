#!/bin/bash

set -eu

docker save protean:latest | \
    xz -T0 -9e | \
    curl -# \
	 -H "Authorization: Bearer ${ZENODO_TOKEN}" \
	 -H "Content-Type: application/octet-stream" \
	 --upload-file - \
	 "${BUCKET_URL}/protean.tar.xz"
