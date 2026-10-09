#!/bin/bash
# Stand-in for LumenOS's system helper (os/system/lumenos-admin) on a development machine: answers
# the settings pages the way a LumenOS system would, without touching anything.
#   LUMEN_OS_ADMIN=tools/mock_lumenos_admin.sh LUMEN_OS_WINDOWED=1 lumen --os
# LUMEN_MOCK_OFFLINE=1: a stick with an update is plugged in.
state="${TMPDIR:-/tmp}/lumenos-mock"
mkdir -p "$state"
case "$1" in
install-list) [ -n "$LUMEN_OS_LIVE" ] && printf '/dev/nvme0n1\t512110190592\tSamsung SSD 980 PRO 500GB\tnvme\n/dev/sda\t2000398934016\tWDC WD20EZAZ\tsata\n' ;;
install-to) sleep 2; echo "installed on $2" ;;
setup-info) echo live=no; echo online=yes ;;
wifi-list) printf 'Heimnetz\t82\tWPA2\tyes\nFRITZ!Box 7590 AB\t64\tWPA2 WPA3\tno\nGast\t40\t--\tno\n' ;;
wifi-connect) sleep 1; [ "$3" = geheim123 ] ;;
share-list) printf 'filme-nas\tsmb\t//nas/filme\tyes\n' ;;
share-add) sleep 1; echo "mount error(13): Permission denied" >&2; exit 1 ;;
bt-scan) sleep 1; printf 'AA:BB:CC:DD:EE:01\tXbox Wireless Controller\tno\tno\nAA:BB:CC:DD:EE:02\tFire TV Remote\tyes\tyes\n' ;;
support-status)
    [ -f "$state/keydb" ] && printf 'keydb=yes\nkeydb_date=2026-10-08\n' || echo keydb=no
    [ -f "$state/dvdcss" ] && echo dvdcss=yes || echo dvdcss=no
    echo aacs=yes; echo "makemkv=$(cat "$state/makemkv" 2> /dev/null)" ;;
keydb-fetch) sleep 1; touch "$state/keydb"; echo "keys for 212345 discs" ;;
keydb-remove) rm -f "$state/keydb" ;;
install-dvdcss) sleep 1; touch "$state/dvdcss" ;;
remove-dvdcss) rm -f "$state/dvdcss" ;;
install-makemkv) sleep 2; echo "1.18.4" > "$state/makemkv" ;;
update-auto) [ "$2" = status ] && cat "$state/auto" 2> /dev/null || { [ "$2" = status ] && echo on || echo "$2" > "$state/auto"; } ;;
update-now) [ -n "$LUMEN_OS_UPDATE_FILE" ] && printf '{"state": "idle", "current": "1.5.0", "latest": "1.5.0", "message": "", "time": %s}\n' "$(date +%s)" > "$LUMEN_OS_UPDATE_FILE" ;;
offline-scan) [ -n "$LUMEN_MOCK_OFFLINE" ] && printf 'package\t1.5.1\t/media/STICK/Lumen-1.5.1-linux-arm64.deb\tnewer\nsource\t1.5.1\t/media/STICK/Lumen-main.zip\tinterface\n' ;;
offline-install) sleep 2; echo ok ;;
reset) rm -rf "$state" ;;
*) exit 0 ;;
esac
