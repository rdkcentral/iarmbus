#!/bin/sh
set -eu

if [ -n "${srcdir:-}" ] && [ -f "$srcdir/../core/libIBusDaemon-dbus.c" ]; then
	source_file="$srcdir/../core/libIBusDaemon-dbus.c"
elif [ -f ../core/libIBusDaemon-dbus.c ]; then
	source_file="../core/libIBusDaemon-dbus.c"
else
	test_srcdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
	source_file="$test_srcdir/../core/libIBusDaemon-dbus.c"
fi

grep -F "memchr(name, '\\0', IARM_MAX_NAME_LEN)" "$source_file" >/dev/null
[ "$(grep -c '_IsValidFixedName' "$source_file")" -ge 8 ]
[ "$(grep -c 'if (registeredMember == NULL)' "$source_file")" -ge 3 ]
if grep -F 'strlen(member->selfName)' "$source_file" >/dev/null; then
	exit 1
fi
for handler in _PowerPreChange _DeepSleepWakeup _ResolutionPreChange _ResolutionPostChange _SysModeChange
do
	sed -n "/static IARM_Result_t ${handler}(void \\*arg)/,/^}/p" "$source_file" |
		grep -F 'if (reqIn == NULL)' >/dev/null
done