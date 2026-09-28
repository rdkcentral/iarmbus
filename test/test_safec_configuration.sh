#!/bin/sh
set -eu
header="${srcdir:-.}/../stubs/safec_lib.h"
grep -F 'if (len > dmax) return ESLEMAX;' "$header" >/dev/null
grep -F 'if (len >= max) return ESLEMAX;' "$header" >/dev/null
if grep -F '#define memcpy_s(dst,max,src,len)  EOK' "$header" >/dev/null; then
    exit 1
fi
