"""Smoke-play every map the list offers from a folder of downloaded map packs (default: your online folder).
python tests/map_list_real.py [--packs output/online/etmain] [--only venice,missile_b4]
Copies the packs into a private profile (originals untouched); each map starts a short local match:
bot maps must load Omni-bot and get bots in, every map must load without a script error.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import time

from legacy_modes import console_command
from online_check import wait_log
from play_game import prepare_game
from smoke_server import ROOT

ORIGINAL = {'pak0.pk3', 'pak1.pk3', 'pak2.pk3', 'mp_bin.pk3'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packs', type=Path, default=ROOT / 'output/online/etmain')
    parser.add_argument('--only', default='')
    args = parser.parse_args()
    modules = ROOT / 'build/slice/Release'
    home = modules / 'smoke/map-list-real'
    shutil.rmtree(home, ignore_errors=True)  # Only this check's private profile.
    command = prepare_game(modules, home)
    for pack in args.packs.glob('*.pk3'):
        if pack.name.lower() not in ORIGINAL:
            shutil.copy2(pack, home / 'etmain' / pack.name)
    mod = home / 'et-rust'
    settings = dict(r_mode=-1, r_customwidth=960, r_customheight=540, r_customaspect=960/540, r_fullscreen=0, r_swapInterval=0,
                    in_mouse=0, in_gamepad=0, s_initsound=0, logfile=2, developer=1, ui_rustMenuTest=1, com_maxfps=60,
                    cl_master='127.0.0.1:9', cl_splitScreen=0, ui_matchPlayers=8)
    with (mod / 'autoexec.cfg').open('a') as config:
        config.write('\n'.join(f'set {k} "{v}"' for k, v in settings.items()) + '\n')
    log = mod / 'etconsole.log'
    client = subprocess.Popen(command, cwd=home, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    results = []
    try:
        wait_log(log, 'MAPLIST maps=', client)
        time.sleep(3)
        listed = [line.split('\t') for line in (mod / 'maplist.txt').read_text(encoding='latin-1').splitlines()]
        only = {name.strip().lower() for name in args.only.split(',') if name.strip()}
        for number, (name, bots, title) in enumerate(listed):
            if only and name.lower() not in only:
                continue
            marker = f'REALMAP_{number}_DONE'
            start = len(log.read_text(encoding='latin-1'))
            (mod / 'real-map.cfg').write_text('\n'.join(['disconnect', 'wait 60', f'set ui_oasisMap {name}', 'ui_test script sliceStart',
                                                         'wait 900', 'mapname', f'set {marker} 1', marker]) + '\n')
            console_command(client, 'exec real-map.cfg')
            deadline = time.monotonic() + 180
            while marker not in log.read_text(encoding='latin-1')[start:]:
                if client.poll() is not None or time.monotonic() > deadline:
                    break
                time.sleep(.5)
            text = log.read_text(encoding='latin-1')[start:]
            loaded = bool(re.search(rf'"mapname" is:"{re.escape(name)}', text, re.I))
            # The previous map's ordinary disconnect is not an error; a broken objective-description file
            # (.objdata, read by the client for the objective texts) is a warning: the match still runs.
            warnings = re.findall(r'ERROR:[^\n]*\.objdata[^\n]*|ERROR: bad objdata[^\n]*', text)
            for warning in warnings:
                text = text.replace(warning, '')
            error = re.search(r'(G_Script_ScriptParse[^\n]*|ERROR:[^\n]*)', text.replace('ERROR: Disconnected from server', ''))
            if warnings:
                print(f'warn {name:24} objective texts: {warnings[0][:120]}', flush=True)
            joined = text.count('entered the game')
            omnibot = 'Omni-bot Loaded Successfully' in text
            ok = loaded and not error and (omnibot and joined >= 4 if bots == '1' else not omnibot)
            results.append((name, bots, ok, loaded, omnibot, joined, error[1] if error else ''))
            print(f'{"ok  " if ok else "FAIL"} {name:24} bots={bots} loaded={loaded} omnibot={omnibot} joined={joined} {error[1] if error else ""}', flush=True)
            if client.poll() is not None:
                break
        console_command(client, 'quit')
        client.wait(timeout=30)
    finally:
        if client.poll() is None:
            client.terminate()
            client.wait(timeout=10)
    failed = [r for r in results if not r[2]]
    print(f'{"PASS" if results and not failed else "FAIL"} real maps: {len(results) - len(failed)}/{len(results)} played. {log}')
    raise SystemExit(1 if failed or not results else 0)


if __name__ == '__main__':
    main()
