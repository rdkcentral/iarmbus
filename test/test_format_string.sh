#!/bin/sh
set -eu
for source_file in "${srcdir:-.}/../core/libIBus-dbus.c" "${srcdir:-.}/../core/libIBus.c"; do
    grep -F 'printf("%s", tmp_buff)' "$source_file" >/dev/null
    if grep -F 'printf(tmp_buff)' "$source_file" >/dev/null; then
        exit 1
    fi
done
