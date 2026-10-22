#!/bin/sh
# Copies the Switch build to the console over FTP, verifies it by SHA-1, and fetches the logs back.
#   switch/deploy.sh <console IP> [push|logs]   (default: push)
# push: out/build/switch/hcu.nro -> sdmc:/switch/hcu/hcu.nro.part, downloads it again, compares the
#       SHA-1 with the local file, then renames it to hcu.nro (the console has gone offline mid-upload). The game data
#       goes once, by hand: game/extracted/ -> sdmc:/switch/hcu/game/ (default.xex, DataXENON/, ArcadeInfo.xml, nxeart).
# logs: hcu_log.txt and hcu_rex_log.txt -> switch/logs/ (git-ignored).
set -e
cd "$(dirname "$0")/.."
IP=$1
ACTION=${2:-push}
[ -n "$IP" ] || { sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
URL="ftp://$IP:${SWITCH_FTP_PORT:-5000}/switch/hcu"
NRO=${SWITCH_BUILD_DIR:-out/build/switch}/hcu.nro
case "$ACTION" in
    push)
        # The NACP (what hbmenu shows) must carry this build's version before anything goes out.
        python3 tools/nro_nacp.py "$NRO" "$(grep -o 'HCU_VERSION "[^"]*"' out/build/switch/hcu_version.h | cut -d'"' -f2)"
        # Upload under a temporary name, read it back, and only then replace hcu.nro: a broken
        # upload never takes the place of a working NRO.
        curl -sS --ftp-create-dirs -T "$NRO" "$URL/hcu.nro.part"
        local_sha=$(sha1sum "$NRO" | cut -c1-40)
        remote_sha=$(curl -sS "$URL/hcu.nro.part" | sha1sum | cut -c1-40)
        if [ "$local_sha" != "$remote_sha" ]; then
            echo "VERIFY FAILED: local $local_sha, console $remote_sha (hcu.nro unchanged)" >&2
            exit 1
        fi
        curl -sS "$URL/" -Q "-DELE hcu.nro" >/dev/null 2>&1 || true
        curl -sS "$URL/" -Q "-RNFR hcu.nro.part" -Q "-RNTO hcu.nro" >/dev/null
        final_sha=$(curl -sS "$URL/hcu.nro" | sha1sum | cut -c1-40)
        [ "$final_sha" = "$local_sha" ] || { echo "RENAME FAILED: hcu.nro is $final_sha" >&2; exit 1; }
        echo "Copied and verified hcu.nro ($local_sha) on $IP, $(grep -o 'HCU_VERSION "[^"]*"' out/build/switch/hcu_version.h)."
        ;;
    logs)
        mkdir -p switch/logs
        for log in hcu_log.txt hcu_rex_log.txt; do
            curl -sS -o "switch/logs/$log" "$URL/$log" && echo "switch/logs/$log" || true
        done
        ;;
    *)
        echo "Unknown action: $ACTION (push or logs)" >&2
        exit 1
        ;;
esac
