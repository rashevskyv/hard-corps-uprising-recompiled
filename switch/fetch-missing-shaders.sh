#!/bin/bash
# Copies the shaders the console's deko3d backend had no DKSH for ("no DKSH for VS|PS <hash>" in hcu_log_*.txt; it
# dumps their ucode into sdmc:/switch/hcu/shaders/missing/) into the shader dump directory, the input of
# switch/stage-shaders.sh (the next switch/build-base.sh or switch/build.sh translates them). Read only on the console:
# its web server's /list and /download.
#   switch/fetch-missing-shaders.sh <console IP> [dump dir, default $HCU_SHADER_DUMPS or /root/hcu/shaders_trace]
# The vertex shaders can come only from here: the game's files hold them unbound (tools/shader_scan.py).
set -eu
IP=${1:?usage: switch/fetch-missing-shaders.sh <console IP> [dump dir]}
DUMPS=${2:-${HCU_SHADER_DUMPS:-/root/hcu/shaders_trace}}
URL="http://$IP"
mkdir -p "$DUMPS"
LIST=$(curl -sS -m 20 "$URL/list?path=%2Fswitch%2Fhcu%2Fshaders%2Fmissing") || { echo "the console's web server ($URL) does not answer" >&2; exit 1; }
NAMES=$(printf '%s' "$LIST" | python3 -I -c 'import json, re, sys
for e in json.load(sys.stdin)["entries"]:
    if re.fullmatch(r"shader_[0-9A-F]{16}\.ucode(\.bin)?\.(vert|frag)", e["name"]): print(e["name"])')
new=0
for n in $NAMES; do
    [ -f "$DUMPS/$n" ] && continue
    curl -sS -m 20 -f -o "$DUMPS/$n.part" "$URL/download?path=/switch/hcu/shaders/missing/$n"
    mv "$DUMPS/$n.part" "$DUMPS/$n"
    new=$((new + 1))
done
echo "$(printf '%s\n' $NAMES | grep -c . || true) files in the console's missing/, $new new in $DUMPS"
