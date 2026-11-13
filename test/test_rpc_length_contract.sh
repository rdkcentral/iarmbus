#!/bin/sh
set -eu

if [ -n "${srcdir:-}" ] && [ -f "$srcdir/core/libIARM-dbus.c" ]; then
	transport="$srcdir/core/libIARM-dbus.c"
	bus="$srcdir/core/libIBus-dbus.c"
elif [ -n "${srcdir:-}" ] && [ -f "$srcdir/../core/libIARM-dbus.c" ]; then
	transport="$srcdir/../core/libIARM-dbus.c"
	bus="$srcdir/../core/libIBus-dbus.c"
else
	test_srcdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
	transport="$test_srcdir/../core/libIARM-dbus.c"
	bus="$test_srcdir/../core/libIBus-dbus.c"
fi

grep -F 'declaredSize != (dbus_uint32_t)arraySize' "$transport" >/dev/null
grep -F 'IARM_GetSize(callArg) != payloadSize' "$transport" >/dev/null
if grep -F 'IARM_GetSize(callArg) > payloadSize' "$transport" >/dev/null; then
	exit 1
fi
grep -F 'callInfo->handler(callInfo->callCtx, (unsigned long)payloadSize' "$transport" >/dev/null
grep -F '_MinimumDaemonArgumentSize' "$bus" >/dev/null
grep -F 'handlerArgSize = env->inner_len' "$bus" >/dev/null
grep -F 'handlerArgSize < cctx->minimumArgSize' "$bus" >/dev/null
grep -F 'retCode = handler(handler_arg)' "$bus" >/dev/null
inner_size_line=$(grep -n -F 'handlerArgSize = env->inner_len' "$bus" | cut -d: -f1)
minimum_check_line=$(grep -n -F 'handlerArgSize < cctx->minimumArgSize' "$bus" | cut -d: -f1)
handler_call_line=$(grep -n -F 'retCode = handler(handler_arg)' "$bus" | cut -d: -f1)
[ "$inner_size_line" -lt "$minimum_check_line" ]
[ "$minimum_check_line" -lt "$handler_call_line" ]