#!/usr/bin/env bash
#set -euo pipefail

# run_demo.sh
# Builds the project, starts the server, records a tcpdump capture, runs the client commands
# and writes fresh logs to client_log.txt and server_log.txt.

echo "Building..."
PORT=4029
PCAP=tsam_capture.pcap
SERVER_BIN=./tsamgroup29
CLIENT_BIN=./tsamclient

CAPTURE=0

usage() {
	cat <<EOF
Usage: $0 [--capture | -c]

By default the demo runs without capturing packets. Use --capture (or -c)
to enable tcpdump capture to $PCAP (requires sudo).
EOF
}

# parse args
while [ "$#" -gt 0 ]; do
	case "$1" in
		-c|--capture)
			CAPTURE=1
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "Unknown arg: $1"
			usage
			exit 1
			;;
	esac
done

echo "Building..."
make

# helper cleanup
SRV_PID=""
TCPD_PID=""
cleanup() {
	[ -n "$TCPD_PID" ] && sudo kill "$TCPD_PID" >/dev/null 2>&1 || true
	[ -n "$SRV_PID" ] && kill "$SRV_PID" >/dev/null 2>&1 || true
	wait "$TCPD_PID" 2>/dev/null || true
	wait "$SRV_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Start server and capture its stdout/stderr
echo "Starting server ($SERVER_BIN $PORT)..."
$SERVER_BIN $PORT > server_log.txt 2>&1 &
SRV_PID=$!
sleep 0.5

if [ "$CAPTURE" -eq 1 ]; then
	echo "Starting tcpdump (will require sudo) to write $PCAP..."
	if ! command -v tcpdump >/dev/null 2>&1; then
		echo "tcpdump not found. Install tcpdump or run without --capture."
		exit 1
	fi
	sudo tcpdump -i any tcp port $PORT -w "$PCAP" >/dev/null 2>&1 &
	TCPD_PID=$!
	sleep 0.5
else
	echo "Packet capture disabled (run with --capture to enable)."
fi

# Run client with a scripted sequence of commands; capture output to client_log.txt
echo "Running client and scripted commands..."
$CLIENT_BIN 127.0.0.1 $PORT <<'EOF' | tee client_log.txt
GETMSG
LISTSERVERS
SENDMSG,A5_29,Hello message nr 1
SENDMSG,A5_29,Hello message nr 2
SENDMSG,A5_29,Hello message nr 3
GETMSG
GETMSG
GETMSG
QUIT
EOF

# Allow logs/packets to flush
sleep 1

echo "Demo complete."
echo "client log -> client_log.txt"
echo "server log -> server_log.txt"
if [ "$CAPTURE" -eq 1 ]; then
	echo "pcap -> $PCAP"
else
	echo "pcap -> (not captured; run with --capture to produce $PCAP)"
fi
