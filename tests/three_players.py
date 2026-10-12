"""Local live-map layout and separate keyboard/controller ownership for every player.
python tests/three_players.py [--players 4]. Uses a private loopback-only profile.
"""
import argparse
import re
import shutil
import subprocess
import time
from PIL import Image, ImageChops, ImageStat
from legacy_modes import console_command
from online_check import wait_log
from play_game import prepare_game
from smoke_server import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--players', type=int, choices=(3, 4), default=3)
    n = parser.parse_args().players
    modules = ROOT / 'build/slice/Release'
    home = modules / f'smoke/input-map-{n}'
    shutil.rmtree(home, ignore_errors=True)
    command = prepare_game(modules, home)
    mod = home / 'et-rust'
    settings = dict(r_mode=-1, r_customwidth=1280, r_customheight=720, r_customaspect=1280/720,
                    r_fullscreen=0, r_swapInterval=0, in_mouse=0, in_gamepad=1, s_initsound=0,
                    developer=1, ui_rustMenuTest=1, logfile=2, com_maxfps=60,
                    cl_splitScreen=n, cl_keyboardPlayer=1, ui_matchPlayers=0, ui_oasisMap='oasis', g_friendlyFire=0)
    for p in range(1, 5):
        settings['in_gamepadIndex' + (str(p) if p > 1 else '')] = -1
        settings[f'ui_oasisTeam{p}'] = 2
        settings[f'ui_oasisClass{p}'] = 2
    with (mod / 'autoexec.cfg').open('a') as config:
        config.write('\n'.join(f'set {k} "{v}"' for k, v in settings.items()) + '\n')
    log = mod / 'etconsole.log'
    client = subprocess.Popen(command, cwd=home, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    serial = 0

    def phase(commands, seconds=90):
        nonlocal serial
        serial += 1
        marker = f'INPUT_MAP_PHASE_{serial}_DONE'
        (mod / 'input-map-phase.cfg').write_text('\n'.join(['set developer 1'] + commands + [f'echo {marker}']) + '\n')
        console_command(client, 'exec input-map-phase.cfg')
        deadline = time.monotonic() + seconds
        while marker not in log.read_text(encoding='latin1'):
            assert client.poll() is None and time.monotonic() < deadline, (marker, log)
            time.sleep(.2)
        return log.read_text(encoding='latin1').split('execing input-map-phase.cfg')[-1]

    def states(text):
        rows = re.findall(r'SPLITPLAYER player=(\d) .* yaw=(-?[\d.]+) weapon=(\d+) menu=(\d)', text)
        return [{int(p): (float(yaw), int(weapon), int(menu)) for p, yaw, weapon, menu in rows[i:i+n]}
                for i in range(0, len(rows), n)]

    def delta(a, b):
        return abs((b - a + 180) % 360 - 180)

    def pad(slot, rx=0):
        return f'gamepad_test {slot} 0 0 {rx} 0 0 1'

    try:
        wait_log(log, 'MAPLIST maps=', client)
        time.sleep(2)
        phase(['wait 30', 'ui_test script sliceStart', 'wait 400', 'devmap oasis', 'wait 600', 'set developer 1'] +
              [f'local_join {p}' for p in range(1, n + 1)] + ['wait 1200'], 120)
        text = log.read_text(encoding='latin1')
        assert f'SPLITSCREEN ready players={",".join(map(str, range(n)))} ai2=0 layout=2x2' in text, log
        assert 'ready player=4' not in text if n == 3 else 'ready player=4' in text
        phase([pad(slot) for slot in range(4)] + ['wait 30', 'screenshot input-map-before'])
        for owner in range(1, n + 1):
            text = phase([f'set cl_keyboardPlayer {owner}', 'wait 10', 'split_state',
                          'split_mouse_test 200 0', 'wait 15', 'split_state',
                          'weaponbank 2', 'wait 90', 'split_state'])
            before, mouse, weapon = states(text)[-3:]
            assert delta(before[owner][0], mouse[owner][0]) > 5, (owner, before, mouse)
            assert all(delta(before[p][0], mouse[p][0]) < .5 for p in before if p != owner), (owner, before, mouse)
            assert weapon[owner][1] in (2, 7) and all(weapon[p][1] == mouse[p][1] for p in before if p != owner), weapon
            # Auto gives slots 0..n-2 to the players other than the keyboard owner, in player order.
            for slot, target in enumerate(p for p in range(1, n + 1) if p != owner):
                text = phase(['split_state', pad(slot, 26000), 'wait 20', pad(slot), 'wait 15', 'split_state'])
                before, after = states(text)[-2:]
                assert delta(before[target][0], after[target][0]) > 5, (owner, slot, target, before, after)
                assert all(delta(before[p][0], after[p][0]) < .5 for p in before if p != target), (before, after)
            # The unused controller must not drive the keyboard player or any other view.
            text = phase(['split_state', pad(n - 1, 26000), 'wait 20', pad(n - 1), 'wait 15', 'split_state'])
            before, after = states(text)[-2:]
            assert all(delta(before[p][0], after[p][0]) < .5 for p in before), (before, after)
        phase(['+back', 'wait 45', '-back', 'wait 30', 'screenshot input-map-after'])
        image = Image.open(mod / 'screenshots/input-map-after.tga').convert('RGB')
        assert image.size == (1280, 720)
        assert all(min(ImageStat.Stat(image.crop((p % 2 * 640, p // 2 * 360, p % 2 * 640 + 640, p // 2 * 360 + 360))).stddev) > 10
                   for p in range(n))
        if n == 3:
            pane = image.crop((640, 360, 1280, 720))
            assert len(pane.getcolors(640 * 360)) > 100, 'Map pane is blank'
            before = Image.open(mod / 'screenshots/input-map-before.tga').convert('RGB').crop((640, 360, 1280, 720))
            assert ImageChops.difference(before, pane).getbbox(), 'Live player markers did not update'
        image.save(ROOT / f'build/input-map-local-{n}.png')
        console_command(client, 'disconnect'); time.sleep(2)
        console_command(client, 'quit'); assert client.wait(timeout=20) == 0
        print(f'PASS {n} local players: separate keyboard/mouse for every slot, isolated controllers and weapon banks, '
              f'2x2 rendering{" with live map" if n == 3 else ""}. {log}')
    finally:
        if client.poll() is None:
            client.terminate(); client.wait(timeout=10)


if __name__ == '__main__':
    main()
