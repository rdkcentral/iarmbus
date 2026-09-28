#!/bin/sh
set -eu
source_file="${srcdir:-.}/../core/libIARM-dbus.c"
grep -F 'dbus_message_get_sender(msg)' "$source_file" >/dev/null
grep -F 'DBUS_INTERFACE_DBUS, "GetNameOwner"' "$source_file" >/dev/null
grep -F 'dbus_connection_send_with_reply_and_block(connection, request, 1000, &error)' "$source_file" >/dev/null
grep -F '"process.iarm.%s.Event"' "$source_file" >/dev/null
grep -F 'if (!isAuthenticatedEventSender(connection, msg, pOwnerName))' "$source_file" >/dev/null
