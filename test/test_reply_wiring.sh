#!/bin/sh
set -eu
source_file="${srcdir:-.}/../core/libIARM-dbus.c"

grep -F 'argCapacity = (uint32_t) IARM_GetSize(arg);' "$source_file" >/dev/null
grep -F 'return IARM_RESULT_INVALID_PARAM;' "$source_file" >/dev/null
grep -F 'IARM_CopyValidatedReply(arg, argCapacity, returnArg, replySize)' "$source_file" >/dev/null
if grep -F 'memcpy_s(arg, size, returnArg, size)' "$source_file" >/dev/null; then
    exit 1
fi
