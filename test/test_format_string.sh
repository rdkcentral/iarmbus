#!/bin/sh
set -eu

if [ -n "${srcdir:-}" ] && [ -f "$srcdir/core/libIBus.c" ]; then
	legacy_source="$srcdir/core/libIBus.c"
	dbus_source="$srcdir/core/libIBus-dbus.c"
elif [ -n "${srcdir:-}" ] && [ -f "$srcdir/../core/libIBus.c" ]; then
	legacy_source="$srcdir/../core/libIBus.c"
	dbus_source="$srcdir/../core/libIBus-dbus.c"
else
	test_srcdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
	legacy_source="$test_srcdir/../core/libIBus.c"
	dbus_source="$test_srcdir/../core/libIBus-dbus.c"
fi

grep -Fq 'ret = printf("%s", tmp_buff);' "$legacy_source"
if grep -Fq 'ret = printf(tmp_buff);' "$legacy_source"; then
	exit 1
fi

grep -Fq 'return printf("%s", tmp_buff);' "$dbus_source"
if grep -Fq 'return printf(tmp_buff);' "$dbus_source"; then
	exit 1
fi