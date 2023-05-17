#!/bin/bash

if false; then
    if [[ -z "${TEST_SUITE_RUN_UNDER+x}" ]]; then
	echo 'TEST_SUITE_RUN_UNDER unset' >&2
	exit 1
    fi
fi

if [[ "${TEST}" = "%s" ]]; then
    echo 'test wasnt substituted' >&2
    exit 1
fi

# ${TEST_SUITE_RUN_UNDER} "$@"
"$@"

