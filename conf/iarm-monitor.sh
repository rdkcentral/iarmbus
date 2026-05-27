#!/bin/sh
# iarm-monitor.sh  <service-name>  <process-name>
#
# Start-phase watchdog launched via ExecStartPre= before the managed
# process starts. Monitors until READY=1 is sent (systemd stops the
# transient unit via ExecStartPost=) or a failure is detected.
#
# Arguments (passed from ExecStartPre= in the service unit):
#   $1  service-name   systemd unit name without .service  (e.g. dsmgr, iarmbusd)
#   $2  process-name   binary name used in rebootNow.sh -c  (e.g. dsMgrMain)

SERVICE_NAME="${1:-unknown-service}"
PROCESS_NAME="${2:-unknown-process}"
LOG_FILE="/opt/logs/uimgr_log.txt"
TAG="[iarm-monitor:${SERVICE_NAME}]"

POLL_INTERVAL=5
MAX_WAIT=180

log() {
    echo "${TAG} $*" >> "${LOG_FILE}"
}

# Send SIGABRT to the process (if still running), wait for breakpad, then reboot.
abort_and_reboot() {
    PID=$(systemctl show "${SERVICE_NAME}.service" -p MainPID --value)
    if [ -n "$PID" ] && [ "$PID" != "0" ]; then
        log "Sending SIGABRT to ${PROCESS_NAME} PID=$PID"
        kill -SIGABRT "$PID"
        # Wait for breakpad to capture and upload minidump before rebooting.
        sleep 5
    else
        log "${PROCESS_NAME} not running — calling rebootNow.sh directly"
    fi
    log "Triggering: /rebootNow.sh -c ${PROCESS_NAME}"
    /rebootNow.sh -c "${PROCESS_NAME}"
}

elapsed=0

while true
do
    RESULT=$(systemctl show "${SERVICE_NAME}.service" -p Result --value)

    if [ "$RESULT" = "timeout" ]; then
        log "${SERVICE_NAME} Result=timeout detected after ${elapsed}s"
        abort_and_reboot
        exit 0
    fi

    if [ "$elapsed" -ge "$MAX_WAIT" ]; then
        log "Monitor: ${elapsed}s elapsed — ${SERVICE_NAME} still not started, sending SIGABRT"
        abort_and_reboot
        exit 0
    fi

    sleep "$POLL_INTERVAL"
    elapsed=$((elapsed + POLL_INTERVAL))
done
