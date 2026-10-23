#!/bin/bash
# Runs the Linux build headless (Xvfb + lavapipe, sound off) and grabs screenshots.
#   scripts/linux-run.sh [seconds, default 90] [extra hcu args...]
#   HCU_KEYS="40:Return,60:space" presses xdotool keys at seconds (held 300 ms) through the SDK's
#   mouse-and-keyboard driver: Return = Start, space = A, BackSpace = B, L = X, P = Y, WASD = left
#   stick, Shift+Up/Down/Left/Right = d-pad (cvars keybind_* of the SDK's mnk driver).
# Paths follow CLAUDE.md "Build layout on this PC": sources /home/user/hcuroot/src, game /home/user/hcuroot/game.
# Output: /home/user/hcuroot/runs/<stamp>/{hcu.log, shot-NN.png, stdout.txt}. One run at a time (kills by PID).
set -u
SECS=${1:-90}; shift || true
ROOT=${HCU_ROOT:-/home/user/hcuroot}
EXE=${HCU_EXE:-$ROOT/src/out/build/linux-amd64-release/hcu}
STAMP=$(date +%Y%m%d-%H%M%S)
OUT=$ROOT/runs/$STAMP
mkdir -p "$OUT"
export SDL_AUDIODRIVER=dummy
# librexruntime.so lands in the SDK tree (CMAKE_RUNTIME_OUTPUT_DIRECTORY), not next to the exe.
export LD_LIBRARY_PATH=$ROOT/rexglue-sdk/out/linux-amd64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}
export DISPLAY=:77
Xvfb :77 -screen 0 1280x720x24 >/dev/null 2>&1 &
XVFB=$!
sleep 1
cd "$(dirname "$EXE")"
"$EXE" --game_data_root="$ROOT/game" --gpu_plugin=xenos --fullscreen=false --audio_mute=true \
    --log_file="$OUT/hcu.log" --log_level="${HCU_LOG_LEVEL:-info}" "$@" >"$OUT/stdout.txt" 2>&1 &
GAME=$!
echo "hcu pid $GAME, output $OUT"
KEYS=${HCU_KEYS:-}
for ((t = 1, n = 0; t <= SECS; t++)); do
    sleep 1
    kill -0 $GAME 2>/dev/null || { echo "hcu exited at ${t}s"; break; }
    for item in ${KEYS//,/ }; do
        if [ "${item%%:*}" = "$t" ]; then
            wid=$(xdotool search --pid $GAME --onlyvisible 2>/dev/null | tail -1)
            [ -n "$wid" ] && xdotool windowfocus --sync "$wid" 2>/dev/null
            xdotool keydown --clearmodifiers "${item#*:}" && sleep 0.3 && xdotool keyup "${item#*:}"
            echo "${t}s: key ${item#*:} (window ${wid:-none})"
        fi
    done
    if (( t % 10 == 5 )); then
        import -window root "$OUT/shot-$(printf %02d $n).png" 2>/dev/null
        n=$((n + 1))
    fi
done
kill $GAME 2>/dev/null; sleep 1; kill -9 $GAME 2>/dev/null
kill $XVFB 2>/dev/null
echo "--- tail of $OUT/hcu.log"; tail -30 "$OUT/hcu.log" 2>/dev/null
echo "--- stdout"; tail -15 "$OUT/stdout.txt"
ls "$OUT"
