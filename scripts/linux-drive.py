#!/usr/bin/env python3
"""Drives the Linux build headless (Xvfb), pressing keys by events in its log instead of by clock.

    scripts/linux-drive.py --steps "waitlog 'Press the START' 900; key Return; wait 40; shot menu" \
        [--exe PATH] [--root /home/user/hcuroot] [--out DIR] [--max 1800] -- <extra hcu args>

Steps (separated by ';'):
    wait <seconds>              sleep
    waitlog <regex> [timeout]   wait until the game's log or stdout has a line matching (default 600 s)
    key <xdotool name> [ms]     press and release (Return = Start, space = A, BackSpace = B, ...; see
                                the SDK's mnk driver cvars keybind_*), held 300 ms by default
    shot <name>                 screenshot of the X root window to <out>/<name>.png (Vulkan backend only)
    end                         stop the game now
The run directory gets hcu.log, stdout.txt and every file the game writes there (frame dumps:
HCU_DK_GL_DUMP=<out>/frames). One run at a time; the game is killed by PID at the end.
"""
import argparse
import os
import re
import shlex
import signal
import subprocess
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--steps', default='')
    ap.add_argument('--exe', default=os.environ.get('HCU_EXE', '/home/user/hcu-switch-private/out/build/linux-testhost/hcu'))
    ap.add_argument('--root', default=os.environ.get('HCU_ROOT', '/home/user/hcuroot'))
    ap.add_argument('--out', default=None)
    ap.add_argument('--max', type=int, default=1800)
    ap.add_argument('--cmdfile', default=None, help='after the steps: run lines appended to this file (same grammar), until "end"')
    ap.add_argument('--display', default=':77')
    ap.add_argument('--log-level', default='info')
    ap.add_argument('game_args', nargs='*')
    a = ap.parse_args()
    out = a.out or os.path.join(a.root, 'runs', time.strftime('%Y%m%d-%H%M%S'))
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ)
    env.update({
        'SDL_AUDIODRIVER': env.get('SDL_AUDIODRIVER', 'pulseaudio'),
        'LD_LIBRARY_PATH': os.path.dirname(a.exe) + ":" + os.environ.get('REXSDK_DIR', '/home/user/rexglue-sdk') + '/out/linux-amd64' + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else ''),
        'VK_ICD_FILENAMES': env.get('VK_ICD_FILENAMES', '/usr/share/vulkan/icd.d/lvp_icd.json'),
        'DISPLAY': a.display,
        'LIBGL_ALWAYS_SOFTWARE': '1',
        'XDG_RUNTIME_DIR': '/tmp/xdg-runtime',
    })
    os.makedirs(env['XDG_RUNTIME_DIR'], exist_ok=True)
    os.chmod(env['XDG_RUNTIME_DIR'], 0o700)
    env.setdefault('HCU_DK_GL_DUMP', os.path.join(out, 'frames') + ':30')
    d = env['HCU_DK_GL_DUMP'].split(':')[0]
    os.makedirs(d, exist_ok=True)
    # A killed game leaves its 4.8 GB guest arena in /dev/shm (tmpfs, 16 GB here): later runs die at start.
    for f in os.listdir('/dev/shm'):
        if f.startswith('xenia_memory_'):
            try:
                os.unlink('/dev/shm/' + f)
            except OSError:
                pass
    xvfb = subprocess.Popen(['Xvfb', a.display, '-screen', '0', '1280x720x24'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    log = os.path.join(out, 'hcu.log')
    stdout = open(os.path.join(out, 'stdout.txt'), 'w')
    cmd = [a.exe, '--game_data_root=' + os.path.join(a.root, 'game'), '--gpu_plugin=xenos', '--fullscreen=false', '--audio_mute=true',
           '--log_file=' + log, '--log_level=' + a.log_level, '--mnk_mode=true', '--headless=true', '--license_mask=4294967295'] + a.game_args
    game = subprocess.Popen(cmd, cwd=os.path.dirname(a.exe), env=env, stdout=stdout, stderr=subprocess.STDOUT)
    print('hcu pid %d, output %s' % (game.pid, out), flush=True)
    t0 = time.time()

    def alive():
        return game.poll() is None and time.time() - t0 < a.max

    def read_all():
        text = ''
        for p in (log, os.path.join(out, 'stdout.txt')):
            try:
                text += open(p, errors='replace').read()
            except OSError:
                pass
        return text

    def key(name, ms=300):
        wid = subprocess.run(['xdotool', 'search', '--pid', str(game.pid), '--onlyvisible'], capture_output=True, text=True, env=env).stdout.split()
        if wid:
            subprocess.run(['xdotool', 'windowfocus', '--sync', wid[-1]], env=env, stderr=subprocess.DEVNULL)
        keys = name.split('+')  # a chord (shift+Down): every key down in order, released in reverse
        for k in keys:
            subprocess.run(['xdotool', 'keydown', k], env=env)
        time.sleep(ms / 1000.0)
        for k in reversed(keys):
            subprocess.run(['xdotool', 'keyup', k], env=env)
        print('%.0fs: key %s (window %s)' % (time.time() - t0, name, wid[-1] if wid else 'none'), flush=True)

    def run_step(step):
        w = shlex.split(step)
        if w[0] == 'wait':
            end = time.time() + float(w[1])
            while time.time() < end and alive():
                time.sleep(0.5)
        elif w[0] == 'waitlog':
            rx = re.compile(w[1])
            end = time.time() + (float(w[2]) if len(w) > 2 else 600)
            found = False
            while time.time() < end and alive():
                if rx.search(read_all()):
                    found = True
                    break
                time.sleep(2)
            print('%.0fs: waitlog %r -> %s' % (time.time() - t0, w[1], 'found' if found else 'TIMEOUT'), flush=True)
        elif w[0] == 'key':
            key(w[1], int(w[2]) if len(w) > 2 else 300)
        elif w[0] == 'shot':
            subprocess.run(['import', '-window', 'root', os.path.join(out, w[1] + '.png')], env=env, stderr=subprocess.DEVNULL)
            print('%.0fs: shot %s' % (time.time() - t0, w[1]), flush=True)
        elif w[0] == 'end':
            return False
        else:
            print('unknown step: ' + step, flush=True)
        return True

    try:
        keep_going = True
        for step in [s.strip() for s in a.steps.split(';') if s.strip()]:
            if not alive():
                print('game gone before step: ' + step, flush=True)
                keep_going = False
                break
            if not run_step(step):
                keep_going = False
                break
        if keep_going and a.cmdfile:
            open(a.cmdfile, 'a').close()
            done_lines = len(open(a.cmdfile).read().splitlines())
            print('waiting for commands in %s' % a.cmdfile, flush=True)
            while alive():
                lines = open(a.cmdfile).read().splitlines()
                for line in lines[done_lines:]:
                    for step in [x.strip() for x in line.split(';') if x.strip()]:
                        if not run_step(step):
                            raise SystemExit
                done_lines = len(lines)
                time.sleep(0.5)
    except SystemExit:
        pass
    finally:
        for p in (game,):
            if p.poll() is None:
                p.send_signal(signal.SIGTERM)
        time.sleep(2)
        if game.poll() is None:
            game.kill()
        xvfb.terminate()
        for f in os.listdir('/dev/shm'):
            if f.startswith('xenia_memory_'):
                try:
                    os.unlink('/dev/shm/' + f)
                except OSError:
                    pass
        print('done after %.0fs (exit %s)' % (time.time() - t0, game.poll()), flush=True)


if __name__ == '__main__':
    main()
