#!/bin/bash
# Prints the app's log from a PS4 running GoldHEN's FTP server (read-only).
#   tools/ps4_log.sh <ps4-ip>          or   PS4_IP=<ps4-ip> tools/ps4_log.sh
IP=${1:-${PS4_IP:-}}
if [ -z "$IP" ]; then
	echo "usage: $0 <ps4-ip>   (or set PS4_IP)" >&2
	exit 1
fi
PORT=${PS4_FTP_PORT:-2121}
curl -s "ftp://$IP:$PORT/data/jellyfin/log.txt"
