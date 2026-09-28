#!/bin/sh
set -eu
header="${srcdir:-.}/../stubs/safec_lib.h"
if grep -F '#define SAFEC_DUMMY_API 1' "$header" >/dev/null; then
    exit 1
fi
grep -F '#error "SAFEC_DUMMY_API disables required bounds checks and is not supported"' "$header" >/dev/null
