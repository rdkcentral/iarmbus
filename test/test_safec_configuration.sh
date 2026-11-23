#!/bin/sh
set -eu

header="${srcdir:-.}/stubs/safec_lib.h"
if [ ! -f "$header" ] || [ ! -r "$header" ]; then
    echo "safec_lib.h is missing or unreadable: $header" >&2
    exit 1
fi

if grep -Eq '^[[:space:]]*#[[:space:]]*define[[:space:]]+SAFEC_DUMMY_API([[:space:]]|$)' "$header"; then
    echo "safec_lib.h must not force dummy safe-C APIs" >&2
    exit 1
fi
