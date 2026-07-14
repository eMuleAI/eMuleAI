#!/bin/bash
set -Eeuo pipefail

APP_DIR="/opt/emuleai"
DEFAULT_CONFIG_DIR="/opt/emuleai-default/config"
DATA_DIR="/data"
CONFIG_DIR="${DATA_DIR}/config"
INCOMING_DIR="${DATA_DIR}/Incoming"
TEMP_DIR="${DATA_DIR}/Temp"
LOG_DIR="${DATA_DIR}/logs"
XPRA_DIR="${DATA_DIR}/xpra"
XPRA_PASSWORD_FILE="${XPRA_DIR}/password.txt"
OWNER_MARKER="${DATA_DIR}/.owner"
DEFAULT_DISPLAY_MODE="xpra"
DEFAULT_DISPLAY_WIDTH=1280
DEFAULT_DISPLAY_HEIGHT=800
DEFAULT_XPRA_CANVAS_WIDTH=7680
DEFAULT_XPRA_CANVAS_HEIGHT=4320
DEFAULT_DISPLAY_DEPTH=24
DEFAULT_XPRA_PORT=14500
NOVNC_DISPLAY=":0"
XPRA_DISPLAY=":100"

is_valid_id() {
	[[ "$1" =~ ^[0-9]+$ ]] && [ "$1" -ge 0 ] && [ "$1" -le 2147483647 ]
}

is_valid_port() {
	[[ "$1" =~ ^[0-9]+$ ]] && [ "$1" -ge 1 ] && [ "$1" -le 65535 ]
}

is_valid_udp_port() {
	[[ "$1" =~ ^[0-9]+$ ]] && [ "$1" -ge 0 ] && [ "$1" -le 65535 ]
}

is_valid_display_dimension() {
	[[ "$1" =~ ^[0-9]+$ ]] && [ "$1" -ge 320 ] && [ "$1" -le 8192 ]
}

is_valid_display_depth() {
	[ "$1" = "16" ] || [ "$1" = "24" ]
}

configure_runtime_user() {
	if ! is_valid_id "${PUID}" || ! is_valid_id "${PGID}"; then
		echo "PUID and PGID must be numeric values between 0 and 2147483647." >&2
		exit 1
	fi

	local target_group="emuleai"
	local existing_group
	existing_group="$(getent group "${PGID}" | cut -d: -f1 || true)"
	if [ -n "${existing_group}" ] && [ "${existing_group}" != "emuleai" ]; then
		target_group="${existing_group}"
	else
		groupmod --non-unique --gid "${PGID}" emuleai
	fi

	usermod --non-unique --uid "${PUID}" --gid "${target_group}" emuleai
	install -d -m 0775 -o "${PUID}" -g "${PGID}" "${DATA_DIR}"
	install -d -m 1777 -o root -g root /tmp/.X11-unix

	local requested_owner="${PUID}:${PGID}"
	local current_owner=""
	if [ -f "${OWNER_MARKER}" ]; then
		current_owner="$(cat "${OWNER_MARKER}" 2>/dev/null || true)"
	fi
	if [ "${current_owner}" != "${requested_owner}" ]; then
		chown -R "${PUID}:${PGID}" "${DATA_DIR}"
		printf '%s\n' "${requested_owner}" > "${OWNER_MARKER}"
		chown "${PUID}:${PGID}" "${OWNER_MARKER}"
	fi

	chown -R "${PUID}:${PGID}" /home/emuleai
	exec gosu emuleai "$0" "$@"
}

seed_config() {
	mkdir -p "${CONFIG_DIR}"
	if [ -d "${DEFAULT_CONFIG_DIR}" ] && [ -z "$(find "${CONFIG_DIR}" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
		cp -a "${DEFAULT_CONFIG_DIR}/." "${CONFIG_DIR}/"
	fi
}

link_persistent_directory() {
	local name="$1"
	local target="$2"
	rm -rf "${APP_DIR:?}/${name}"
	ln -s "${target}" "${APP_DIR}/${name}"
}

show_startup_log() {
	local log_file="$1"
	if [ -s "${log_file}" ]; then
		echo "--- ${log_file} ---" >&2
		tail -n 80 "${log_file}" >&2
	fi
}

wait_for_x_server() {
	local display_name="$1"
	local server_pid="$2"
	local log_file="$3"
	local display_number="${display_name#:}"
	display_number="${display_number%%.*}"
	local attempt
	for attempt in $(seq 1 300); do
		if [ -S "/tmp/.X11-unix/X${display_number}" ]; then
			return 0
		fi
		if ! kill -0 "${server_pid}" 2>/dev/null; then
			echo "X server ${display_name} exited during startup." >&2
			show_startup_log "${log_file}"
			return 1
		fi
		sleep 0.1
	done
	echo "X server ${display_name} did not become ready." >&2
	show_startup_log "${log_file}"
	return 1
}

start_xvfb_display() {
	local display_name="$1"
	local display_width="$2"
	local display_height="$3"
	local log_file="/tmp/xvfb.log"
	local display_number="${display_name#:}"
	display_number="${display_number%%.*}"
	rm -f "/tmp/.X${display_number}-lock" "/tmp/.X11-unix/X${display_number}"
	mkdir -p /tmp/.X11-unix

	Xvfb "${display_name}" -screen 0 "${display_width}x${display_height}x${DISPLAY_DEPTH}" -nolisten tcp -ac +extension GLX +render -noreset >"${log_file}" 2>&1 &
	XVFB_PID=$!
	wait_for_x_server "${display_name}" "${XVFB_PID}" "${log_file}"
}

prepare_xpra_password() {
	mkdir -p "${XPRA_DIR}"
	umask 077

	if [ -n "${XPRA_PASSWORD:-}" ]; then
		if [[ "${XPRA_PASSWORD}" == *$'\n'* ]] || [[ "${XPRA_PASSWORD}" == *$'\r'* ]]; then
			echo "XPRA_PASSWORD must not contain line breaks." >&2
			exit 1
		fi
		printf '%s' "${XPRA_PASSWORD}" > "${XPRA_PASSWORD_FILE}"
	elif [ ! -s "${XPRA_PASSWORD_FILE}" ]; then
		python3 - "${XPRA_PASSWORD_FILE}" <<'PY'
import secrets
import string
import sys

alphabet = string.ascii_letters + string.digits
password = "".join(secrets.choice(alphabet) for _ in range(32))
with open(sys.argv[1], "w", encoding="ascii", newline="") as handle:
    handle.write(password)
PY
	fi

	XPRA_PASSWORD="$(cat "${XPRA_PASSWORD_FILE}")"
	if [ -z "${XPRA_PASSWORD}" ] || [[ "${XPRA_PASSWORD}" == *$'\n'* ]] || [[ "${XPRA_PASSWORD}" == *$'\r'* ]]; then
		echo "The Xpra password file must contain one non-empty value without line breaks." >&2
		exit 1
	fi
	export XPRA_PASSWORD
	chmod 0600 "${XPRA_PASSWORD_FILE}"
	echo "Xpra password file: ${XPRA_PASSWORD_FILE}"
}

start_novnc_display() {
	export DISPLAY="${NOVNC_DISPLAY}"
	start_xvfb_display "${DISPLAY}" "${DISPLAY_WIDTH}" "${DISPLAY_HEIGHT}"

	openbox --sm-disable >/tmp/openbox.log 2>&1 &
	OPENBOX_PID=$!

	x11vnc -display "${DISPLAY}" -rfbport 5900 -localhost -forever -shared -nopw -quiet >/tmp/x11vnc.log 2>&1 &
	VNC_PID=$!

	websockify --web=/usr/share/novnc "${NOVNC_PORT}" localhost:5900 >/tmp/websockify.log 2>&1 &
	WEBSOCKIFY_PID=$!

	echo "Display mode: noVNC"
	echo "Browser URL: http://localhost:${NOVNC_PORT}/vnc.html?autoconnect=1&resize=scale"
}

start_xpra_display() {
	export DISPLAY="${XPRA_DISPLAY}"
	prepare_xpra_password
	start_xvfb_display "${DISPLAY}" "${XPRA_CANVAS_WIDTH}" "${XPRA_CANVAS_HEIGHT}"

	xpra start "${DISPLAY}" \
		--use-display=yes \
		--resize-display=auto \
		--daemon=no \
		--systemd-run=no \
		--bind=none \
		--bind-tcp="0.0.0.0:${XPRA_PORT}" \
		--tcp-auth=env \
		--mdns=no \
		--dbus-launch=no \
		--pulseaudio=no \
		--webcam=no \
		--printing=no \
		--file-transfer=no \
		--open-files=no \
		--clipboard=yes \
		--notifications=no \
		--start-new-commands=no \
		--exit-with-client=no \
		--exit-with-windows=no \
		>/tmp/xpra.log 2>&1 &
	XPRA_PID=$!

	local port_hex
	printf -v port_hex '%04X' "${XPRA_PORT}"
	local xpra_ready=0
	local attempt
	for attempt in $(seq 1 300); do
		if ! kill -0 "${XPRA_PID}" 2>/dev/null; then
			echo "Xpra server exited during startup." >&2
			show_startup_log /tmp/xpra.log
			return 1
		fi
		if awk -v port=":${port_hex}" '$4 == "0A" && substr($2, length($2) - 4) == port { found=1 } END { exit !found }' /proc/net/tcp; then
			xpra_ready=1
			break
		fi
		sleep 0.1
	done
	if [ "${xpra_ready}" -ne 1 ]; then
		echo "Xpra server did not start listening on TCP port ${XPRA_PORT}." >&2
		show_startup_log /tmp/xpra.log
		return 1
	fi

	echo "Display mode: Xpra seamless"
	echo "Xpra canvas: ${XPRA_CANVAS_WIDTH}x${XPRA_CANVAS_HEIGHT}x${DISPLAY_DEPTH}"
	echo "Xpra endpoint: tcp://127.0.0.1:${XPRA_PORT}"
}

close_emule() {
	if [ -z "${APP_PID:-}" ] || ! kill -0 "${APP_PID}" 2>/dev/null; then
		return
	fi

	xdotool search --onlyvisible --class "emuleai.exe" windowclose 2>/dev/null || \
		xdotool search --onlyvisible --name "eMule" windowclose 2>/dev/null || true

	local attempt
	for attempt in $(seq 1 20); do
		if ! kill -0 "${APP_PID}" 2>/dev/null; then
			return
		fi
		sleep 1
	done

	wineserver -k >/dev/null 2>&1 || true
}

cleanup() {
	for pid in "${WEBSOCKIFY_PID:-}" "${VNC_PID:-}" "${OPENBOX_PID:-}" "${XPRA_PID:-}" "${XVFB_PID:-}"; do
		if [ -n "${pid}" ]; then
			kill "${pid}" 2>/dev/null || true
		fi
	done
}

on_terminate() {
	TERMINATING=1
	close_emule
}

if [ "$(id -u)" -eq 0 ]; then
	configure_runtime_user "$@"
fi

DISPLAY_MODE="${DISPLAY_MODE,,}"
case "${DISPLAY_MODE}" in
	novnc|xpra)
		;;
	*)
		echo "Invalid DISPLAY_MODE '${DISPLAY_MODE}'. Using default ${DEFAULT_DISPLAY_MODE}." >&2
		DISPLAY_MODE="${DEFAULT_DISPLAY_MODE}"
		;;
esac

if ! is_valid_port "${EMULE_TCP_PORT}" || ! is_valid_udp_port "${EMULE_UDP_PORT}"; then
	echo "EMULE_TCP_PORT must be 1-65535. EMULE_UDP_PORT must be 0-65535." >&2
	exit 1
fi
if [ "${DISPLAY_MODE}" = "novnc" ] && ! is_valid_port "${NOVNC_PORT}"; then
	echo "NOVNC_PORT must be 1-65535." >&2
	exit 1
fi
if [ "${DISPLAY_MODE}" = "xpra" ] && ! is_valid_port "${XPRA_PORT}"; then
	echo "Invalid XPRA_PORT '${XPRA_PORT}'. Using default ${DEFAULT_XPRA_PORT}." >&2
	XPRA_PORT="${DEFAULT_XPRA_PORT}"
fi

if ! is_valid_display_dimension "${DISPLAY_WIDTH}"; then
	echo "Invalid DISPLAY_WIDTH '${DISPLAY_WIDTH}'. Using default ${DEFAULT_DISPLAY_WIDTH}." >&2
	DISPLAY_WIDTH="${DEFAULT_DISPLAY_WIDTH}"
fi
if ! is_valid_display_dimension "${DISPLAY_HEIGHT}"; then
	echo "Invalid DISPLAY_HEIGHT '${DISPLAY_HEIGHT}'. Using default ${DEFAULT_DISPLAY_HEIGHT}." >&2
	DISPLAY_HEIGHT="${DEFAULT_DISPLAY_HEIGHT}"
fi
if ! is_valid_display_dimension "${XPRA_CANVAS_WIDTH}"; then
	echo "Invalid XPRA_CANVAS_WIDTH '${XPRA_CANVAS_WIDTH}'. Using default ${DEFAULT_XPRA_CANVAS_WIDTH}." >&2
	XPRA_CANVAS_WIDTH="${DEFAULT_XPRA_CANVAS_WIDTH}"
fi
if ! is_valid_display_dimension "${XPRA_CANVAS_HEIGHT}"; then
	echo "Invalid XPRA_CANVAS_HEIGHT '${XPRA_CANVAS_HEIGHT}'. Using default ${DEFAULT_XPRA_CANVAS_HEIGHT}." >&2
	XPRA_CANVAS_HEIGHT="${DEFAULT_XPRA_CANVAS_HEIGHT}"
fi
if ! is_valid_display_depth "${DISPLAY_DEPTH}"; then
	echo "Invalid DISPLAY_DEPTH '${DISPLAY_DEPTH}'. Using default ${DEFAULT_DISPLAY_DEPTH}." >&2
	DISPLAY_DEPTH="${DEFAULT_DISPLAY_DEPTH}"
fi

if [ ! -f "${APP_DIR}/eMuleAI.exe" ]; then
	echo "${APP_DIR}/eMuleAI.exe is missing." >&2
	exit 1
fi
if [ "${DISPLAY_MODE}" = "xpra" ] && ! command -v xpra >/dev/null 2>&1; then
	echo "Xpra is not installed in the image." >&2
	exit 1
fi

mkdir -p "${INCOMING_DIR}" "${TEMP_DIR}" "${LOG_DIR}" "${WINEPREFIX}"
export XDG_RUNTIME_DIR="/tmp/runtime-emuleai"
mkdir -p "${XDG_RUNTIME_DIR}"
chmod 0700 "${XDG_RUNTIME_DIR}"
seed_config
link_persistent_directory "config" "${CONFIG_DIR}"
link_persistent_directory "Incoming" "${INCOMING_DIR}"
link_persistent_directory "Temp" "${TEMP_DIR}"
link_persistent_directory "logs" "${LOG_DIR}"

/usr/local/bin/configure-settings.py \
	"${CONFIG_DIR}/preferences.ini" \
	"${EMULE_TCP_PORT}" \
	"${EMULE_UDP_PORT}"

TERMINATING=0
APP_PID=""
trap on_terminate TERM INT
trap cleanup EXIT

if [ "${DISPLAY_MODE}" = "xpra" ]; then
	start_xpra_display
else
	start_novnc_display
fi

wineboot -u >/tmp/wineboot.log 2>&1
wine "${APP_DIR}/eMuleAI.exe" &
APP_PID=$!

set +e
wait "${APP_PID}"
APP_STATUS=$?
set -e

if [ "${TERMINATING}" -eq 1 ]; then
	exit 0
fi
exit "${APP_STATUS}"
