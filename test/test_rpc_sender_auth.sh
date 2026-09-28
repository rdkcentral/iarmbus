#!/bin/sh
set -eu
transport="${srcdir:-.}/../core/libIARM-dbus.c"
bus="${srcdir:-.}/../core/libIBus-dbus.c"
grep -F 'callInfo->handler(callInfo->callCtx, (unsigned long)connection' "$transport" >/dev/null
grep -F 'dbus_bus_list_names(connection, &error)' "$bus" >/dev/null
grep -F 'dbus_message_get_sender(message)' "$bus" >/dev/null
grep -F 'if (!_IsRegisteredIarmCaller(connection, message))' "$bus" >/dev/null
