#!/bin/sh
##########################################################################
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2016 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################

# iarm-reboot.sh  <service-name>  <process-name>
#
# Called by systemd via ExecStopPost= after a managed process exits for
# ANY reason (clean stop, crash, timeout, or non-zero exit).
#
# Arguments (passed from ExecStopPost= in the service unit):
#   $1  service-name   systemd unit name without .service  (e.g. dsmgr, iarmbusd)
#   $2  process-name   binary name used in rebootNow.sh -c  (e.g. dsMgrMain)
#
# Systemd sets $SERVICE_RESULT when invoking ExecStopPost:
#   "success"   — clean exit (return 0 or SIGTERM from systemctl stop)
#   "signal"    — killed by unhandled signal (SIGSEGV, SIGABRT, etc.)
#   "exit-code" — non-zero exit
#   "timeout"   — watchdog / start timeout expired
#   "core-dump" — process produced a core dump
#
# Only trigger a device reboot when the exit was abnormal.  A clean
# systemctl stop (SERVICE_RESULT=success) does NOT reboot.

SERVICE_NAME="${1:-unknown-service}"
PROCESS_NAME="${2:-unknown-process}"
LOG_FILE="/opt/logs/uimgr_log.txt"
TAG="[iarm-reboot:${SERVICE_NAME}]"

EXIT_CODE="${EXIT_CODE:-0}"
EXIT_STATUS="${EXIT_STATUS:-}"

# $SERVICE_RESULT is only injected by systemd >= v232.  On older systemd (this
# platform runs v230) it is empty, so query the result from systemd directly.
# NOTE: --value flag was added in v230; use Result=xxx parse as primary to be safe.
if [ -z "${SERVICE_RESULT}" ] || [ "${SERVICE_RESULT}" = "unknown" ]; then
    _raw=$(systemctl show "${SERVICE_NAME}" --property=Result 2>/dev/null)
    # _raw is "Result=success" / "Result=timeout" etc.
    SERVICE_RESULT=$(echo "${_raw}" | sed 's/^Result=//')
    # If sed left it unchanged (no match) or empty, flag as unknown
    [ "${SERVICE_RESULT}" = "${_raw}" ] && SERVICE_RESULT=""
    SERVICE_RESULT="${SERVICE_RESULT:-unknown}"
fi

echo "${TAG} ${PROCESS_NAME} exited: SERVICE_RESULT=${SERVICE_RESULT}" \
     "EXIT_CODE=${EXIT_CODE} EXIT_STATUS=${EXIT_STATUS}" >&2

case "${SERVICE_RESULT}" in
    success)
        # Clean stop — systemctl stop or process returned 0. No reboot needed.
        echo "${TAG} Clean exit — no reboot triggered." >&2
        rm -f "/tmp/${SERVICE_NAME}.ready"
        exit 0
        ;;
    unknown)
        # systemctl show could not determine the result (very old systemd or
        # dbus not available).  Last resort: use EXIT_CODE + EXIT_STATUS.
        #
        # Start-timeout case: systemd kills dsMgrMain with SIGTERM, so
        #   EXIT_CODE=0  but  EXIT_STATUS=TERM (or 15).
        # Clean systemctl stop: also SIGTERM, indistinguishable here.
        # We therefore check the sentinel created by ExecStartPost:
        #   sentinel present  → process had fully started → clean stop → no reboot
        #   sentinel absent   → never reached ready       → start-timeout → reboot
        SENTINEL="/tmp/${SERVICE_NAME}.ready"
        if [ "${EXIT_CODE}" = "0" ]; then
            if [ -f "${SENTINEL}" ]; then
                echo "${TAG} Result unknown; EXIT_CODE=0; sentinel present" \
                     "— treating as clean exit, no reboot." >&2
                rm -f "${SENTINEL}"
                exit 0
            else
                echo "${TAG} Result unknown; EXIT_CODE=0; sentinel absent" \
                     "— ${PROCESS_NAME} never fully started (start-timeout or early failure)" \
                     "— triggering reboot." >&2
            fi
        else
            echo "${TAG} Result unknown; EXIT_CODE=${EXIT_CODE}" \
                 "— treating as abnormal exit, triggering reboot." >&2
        fi
        rm -f "${SENTINEL}"
        ;;
    signal|core-dump)
        # Crashed by an unhandled signal (SIGSEGV=11, SIGABRT=6, etc.)
        echo "${TAG} Crash detected (${SERVICE_RESULT} / ${EXIT_STATUS})" \
             "— triggering reboot via rebootNow.sh" >&2
        ;;
    exit-code|timeout|*)
        # Non-zero exit or watchdog timeout — treat as abnormal.
        echo "${TAG} Abnormal exit (${SERVICE_RESULT} / code=${EXIT_CODE})" \
             "— triggering reboot via rebootNow.sh" >&2
        ;;
esac

# Reboot storm protection — mirrors RDK-v reboot-count-checker.sh logic.
# Counter /opt/.dsmgr_restart_count persists across reboots; suppresses
# reboot after 10 consecutive failures. Coredump wait skipped (breakpad
# handles it asynchronously on RDK-e).
# NOTE: Counter is incremented only on abnormal exit (signal, exit-code,
# timeout, unknown). Clean exit (success) returns early above and never
# reaches this point.
COUNTER_FILE="/opt/.${SERVICE_NAME}_restart_count"
MAX_REBOOTS=10

# Read and increment counter (matches RDK-v: expr $count + 1 logic)
if [ ! -f "${COUNTER_FILE}" ]; then
    count=1
else
    count=$(cat "${COUNTER_FILE}" 2>/dev/null)
    case "${count}" in
        ''|*[!0-9]*)
            count=0
            ;;
    esac
    count=$(expr "${count}" + 1)
fi
echo "${count}" > "${COUNTER_FILE}"

echo "${TAG} ${PROCESS_NAME} restart count: ${count}/${MAX_REBOOTS}" >&2
echo "${TAG} ${PROCESS_NAME} restart count: ${count}/${MAX_REBOOTS}" >> "${LOG_FILE}"

if [ "${count}" -gt "${MAX_REBOOTS}" ]; then
    # Mirrors: "-----Box has rebooted 10 times.. no more reboot----"
    # Exit 0: suppressing reboot is intentional, not an error — avoids
    # marking the ExecStopPost as failed in systemd unit state.
    echo "${TAG} Box has rebooted ${MAX_REBOOTS} times — no more reboot." >&2
    echo "${TAG} Box has rebooted ${MAX_REBOOTS} times — no more reboot." >> "${LOG_FILE}"
    rm -f "/tmp/${SERVICE_NAME}.ready"
    exit 0
fi

# Mirrors: check "Dependency failed" then pick -s or -c flag for rebootNow.sh
if systemctl -l status "${SERVICE_NAME}" 2>/dev/null | grep -qi "Dependency failed"; then
    echo "${TAG} Dependency failure detected." >&2
    REBOOT_ARGS="-s ${PROCESS_NAME} -o due_to_service_dependency_failure"
else
    # -c indicates a crash reboot (mirrors RDK-v: /rebootNow.sh -c <process>)
    REBOOT_ARGS="-c ${PROCESS_NAME}"
fi

# Remove the ready sentinel before rebooting.
rm -f "/tmp/${SERVICE_NAME}.ready"

echo "${TAG} Triggering: /rebootNow.sh ${REBOOT_ARGS}" >&2

if [ -x /rebootNow.sh ]; then
    exec /rebootNow.sh ${REBOOT_ARGS}
else
    echo "${TAG} ERROR: /rebootNow.sh not found or not executable" >&2
    # Fall back to a hard reboot if the script is missing.
    /sbin/reboot
fi
