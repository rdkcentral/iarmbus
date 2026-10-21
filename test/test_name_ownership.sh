#!/bin/sh
set -eu
source_file="${srcdir:-.}/../core/libIARM-dbus.c"
count=$(grep -c 'dbus_bus_request_name.*DBUS_NAME_FLAG_DO_NOT_QUEUE' "$source_file")
[ "$count" -eq 3 ]
if grep -E 'dbus_bus_request_name.*(DBUS_NAME_FLAG_ALLOW_REPLACEMENT|DBUS_NAME_FLAG_REPLACE_EXISTING)' "$source_file" >/dev/null; then
    exit 1
fi
