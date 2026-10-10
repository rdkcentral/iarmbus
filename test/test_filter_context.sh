#!/bin/sh
set -eu
source_file="${srcdir:-.}/../core/libIARM-dbus.c"
grep -F 'callInfo->type = IARM_FILTER_METHOD;' "$source_file" >/dev/null
grep -F 'eventInfo->type = IARM_FILTER_EVENT;' "$source_file" >/dev/null
grep -F 'if (filterInfo->type != IARM_FILTER_EVENT)' "$source_file" >/dev/null
grep -F 'if (filterInfo->type != IARM_FILTER_METHOD)' "$source_file" >/dev/null
if grep -F 'IARM_UICall_t *callInfo = (IARM_UICall_t *)user_data;' "$source_file" >/dev/null; then
    exit 1
fi
