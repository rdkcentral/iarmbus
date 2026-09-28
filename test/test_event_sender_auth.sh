#!/bin/sh
set -eu
source_file="${srcdir:-.}/../core/libIARM-dbus.c"
grep -F 'dbus_message_get_sender(msg)' "$source_file" >/dev/null
grep -F 'dbus_bus_get_name_owner(connection, serviceName, &error)' "$source_file" >/dev/null
grep -F '"process.iarm.%s.Event"' "$source_file" >/dev/null
grep -F 'if (!isAuthenticatedEventSender(connection, msg, pOwnerName))' "$source_file" >/dev/null
