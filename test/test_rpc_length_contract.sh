#!/bin/sh
set -eu
transport="${srcdir:-.}/../core/libIARM-dbus.c"
bus="${srcdir:-.}/../core/libIBus-dbus.c"
grep -F 'declaredSize != arraySize' "$transport" >/dev/null
grep -F 'IARM_GetSize(callArg) > payloadSize' "$transport" >/dev/null
grep -F 'callInfo->handler(callInfo->callCtx, (unsigned long)payloadSize' "$transport" >/dev/null
grep -F 'methodID < cctx->minimumArgSize' "$bus" >/dev/null
grep -F '_MinimumDaemonArgumentSize' "$bus" >/dev/null
