#!/bin/sh
set -eu

if [ -n "${srcdir:-}" ] && [ -f "$srcdir/core/libIARM-dbus.c" ]; then
	source_file="$srcdir/core/libIARM-dbus.c"
elif [ -n "${srcdir:-}" ] && [ -f "$srcdir/../core/libIARM-dbus.c" ]; then
	source_file="$srcdir/../core/libIARM-dbus.c"
else
	test_srcdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
	source_file="$test_srcdir/../core/libIARM-dbus.c"
fi

line_of() {
	grep -nF "$1" "$source_file" | cut -d: -f1
}

capacity_line=$(line_of 'argCapacity = IARM_GetSize(arg);')
bounds_line=$(line_of 'IARM_CopyValidatedReply(arg, argCapacity, returnArg, replySize)')
return_line=$(line_of '*ret = replyCode;')

[ "$capacity_line" -lt "$bounds_line" ]
[ "$bounds_line" -lt "$return_line" ]
grep -Fq 'dbus_message_iter_get_arg_type(&arglist) != DBUS_TYPE_INT32' "$source_file"
grep -Fq 'dbus_message_iter_get_element_type(&arglist) != DBUS_TYPE_BYTE' "$source_file"
grep -Fq 'if (dbus_message_iter_next(&arglist) ||' "$source_file"
if grep -Fq 'dbus_message_iter_get_basic(&arglist, ret)' "$source_file"; then
	exit 1
fi