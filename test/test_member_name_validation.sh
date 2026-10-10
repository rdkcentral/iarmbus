#!/bin/sh
set -eu
source_file="${srcdir:-.}/../core/libIBusDaemon-dbus.c"
grep -F "memchr(name, '\\0', IARM_MAX_NAME_LEN)" "$source_file" >/dev/null
[ "$(grep -c '_IsValidFixedName' "$source_file")" -ge 8 ]
[ "$(grep -c 'if (registeredMember == NULL)' "$source_file")" -ge 3 ]
if grep -F 'strlen(member->selfName)' "$source_file" >/dev/null; then
    exit 1
fi
