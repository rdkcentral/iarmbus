#!/bin/sh
set -eu

if [ -n "${srcdir:-}" ] && [ -f "$srcdir/core/libIBus-dbus.c" ]; then
	dbus_source="$srcdir/core/libIBus-dbus.c"
elif [ -n "${srcdir:-}" ] && [ -f "$srcdir/../core/libIBus-dbus.c" ]; then
	dbus_source="$srcdir/../core/libIBus-dbus.c"
else
	test_srcdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
	dbus_source="$test_srcdir/../core/libIBus-dbus.c"
fi

grep -Fq 'return printf("%s", tmp_buff);' "$dbus_source"
if grep -Fq 'return printf(tmp_buff);' "$dbus_source"; then
	exit 1
fi