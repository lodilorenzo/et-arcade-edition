"""Player setup: per-player profiles (XP kept between local matches), controller and keyboard mapping.
python tests/players_setup.py. Opens a game window; local loopback match only, no bots.
"""
import math
import re
import shutil
import subprocess
import time

from PIL import Image
from legacy_modes import console_command
from online_check import wait_log
from play_game import prepare_game
from smoke_server import ROOT


def main():
    modules = ROOT / 'build/slice/Release'
    home = modules / 'smoke/players-setup'
    shutil.rmtree(home, ignore_errors=True)  # Only this check's private profile.
    command = prepare_game(modules, home)
    mod = home / 'et-rust'
    settings = dict(r_mode=-1, r_customwidth=1280, r_customheight=720, r_customaspect=1280/720, r_fullscreen=0, r_swapInterval=0,
                    in_mouse=0, in_joystick=0, in_gamepad=1, s_initsound=0, logfile=2, developer=1, ui_rustMenuTest=1, com_maxfps=60,
                    cl_master='127.0.0.1:9', cl_splitScreen=1, ui_matchPlayers=0, ui_oasisMap='oasis', g_doWarmup=0,
                    in_gamepadIndex=0, in_gamepadIndex2=-1, cl_keyboardPlayer=1,
                    ui_oasisTeam1=2, ui_oasisClass1=2, ui_oasisTeam2=2, ui_oasisClass2=1)
    with (mod / 'autoexec.cfg').open('a') as config:
        config.write('\n'.join(f'set {k} "{v}"' for k, v in settings.items()) + '\n')
    log = mod / 'etconsole.log'
    client = subprocess.Popen(command, cwd=home, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    serial = 0

    def phase(commands, seconds=60):
        nonlocal serial
        serial += 1
        marker = f'PLAYERS_PHASE_{serial}_DONE'
        (mod / 'players-phase.cfg').write_text('\n'.join(commands + [f'set {marker} 1', marker]) + '\n')
        console_command(client, 'exec players-phase.cfg')
        deadline = time.monotonic() + seconds
        while marker not in log.read_text(encoding='latin-1'):
            assert time.monotonic() < deadline and client.poll() is None, (marker, log)
            time.sleep(.2)
        return log.read_text(encoding='latin-1').split('execing players-phase.cfg')[-1]

    def pads(text):
        return re.findall(r'PAD_CHECK menu="([^"]*)" item="([^"]*)" cvar="([^"]*)" value="([^"]*)" editing=(\d)', text)

    def key(name, count=1):
        return [f'ui_test pad {name}'] * count

    try:
        wait_log(log, 'MAPLIST maps=', client)
        time.sleep(2)
        # Main menu -> Players & Controllers.
        first = pads(phase(['wait 30', 'ui_test pad state']))[-1][1]
        order = ['PLAY ONLINE', 'START MATCH', 'Players:', 'Map:', 'Total players:', 'PLAYERS & CONTROLLERS']
        text = phase(key('down', order.index('PLAYERS & CONTROLLERS') - order.index(first)) + key('a') + ['wait 20'] + key('state'))  # Opens focused on player 1's profile.
        assert pads(text)[-1][:2] == ('players_setup', 'Profile:'), pads(text)
        # Player 2: Controller 3 (Auto -> 1 -> 2 -> 3); keyboard & mouse to player 2.
        text = phase(key('down', 3) + key('right', 3) + key('state') + key('down', 5) + key('right') + key('state'))
        rows = pads(text)
        assert rows[-2][:4] == ('players_setup', 'Controller:', 'in_gamepadindex2', '2'), rows
        assert rows[-1][:4] == ('players_setup', 'Keyboard & mouse:', 'cl_keyboardplayer', '2'), rows
        # Type "AB" with the pad: A edits, Up adds 'A', Right + Up adds another, Up turns it to 'B', A accepts.
        text = phase(key('down') + key('a') + key('up') + key('right') + key('up') + key('up') + key('state') + key('a') + key('state'))
        rows = pads(text)
        assert rows[-2][3] == 'AB' and rows[-2][4] == '1' and rows[-1][3] == 'AB' and rows[-1][4] == '0', rows
        text = phase(key('down') + key('right') + key('state') + key('down') + key('a') + ['wait 10', 'ui_localProfile2', 'ui_rustPad2',
                     'wait 30', 'screenshot players-setup'])
        assert pads(text)[-1][3] == '2', pads(text)
        assert 'LOCALPROFILE created id=ab player=2' in text and re.search(r'"ui_localProfile2" is:"ab', text), text[-2000:]
        assert re.search(r'"ui_rustPad2" is:"Keyboard & mouse', text), text[-2000:]
        Image.open(mod / 'screenshots/players-setup.tga').save(ROOT / 'build/players-setup.png')
        # Player 1's profile through the same create script.
        phase(['set ui_rustNewProfile "Alpha One"', 'set ui_rustProfileTarget 1', 'ui_test script sliceProfileCreate'])
        assert (mod / 'localprofiles/ab.dat').exists() and (mod / 'localprofiles/alpha_one.dat').exists()
        # Match 1: names from the profiles; XP survives a round restart and is saved on leaving.
        match = ['ui_test script sliceStart', 'wait 400', 'local_join 1', 'local_join 2', 'wait 200']
        text = phase(['ui_test pad b', 'wait 10'] + match + ['localprofile_test'], 120)
        state = dict((int(c), (s, p, n)) for c, s, p, n in re.findall(r'LOCALPROFILE_TEST client=(\d+) slot=(\d) profile=(\S*) name=(\S[^\n]*?) xp=', text))
        one = next(c for c, v in state.items() if v[0] == '1'); two = next(c for c, v in state.items() if v[0] == '2')
        assert state[one][1:] == ('alpha_one', 'Alpha One') and state[two][1:] == ('ab', 'AB'), state
        # Keyboard/mouse drive player 2 exclusively; its saved controller choice is ignored. Controller 1 drives player 1.
        def snapshots(text):
            # Each split_state prints one SPLITPLAYER row per player, in order (commands are not echoed).
            rows = re.findall(r'SPLITPLAYER player=(\d) .* pos=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+) .* yaw=(-?[\d.]+)', text)
            return [{int(r[0]): tuple(map(float, r[1:])) for r in rows[i:i + 2]} for i in range(0, len(rows) - 1, 2)]
        def moved(a, b):
            return math.dist(a[:3], b[:3])
        def turned(a, b):
            return abs((b[3] - a[3] + 180) % 360 - 180)
        text = phase(['split_state', '+forward', 'wait 30', '-forward', 'split_mouse_test 300 0', 'wait 30', 'split_state'])
        before, after = snapshots(text)[-2:]
        assert moved(before[2], after[2]) > 20 and turned(before[2], after[2]) > 5, (before, after)
        assert moved(before[1], after[1]) < 1 and turned(before[1], after[1]) < .5, (before, after)
        pad = lambda slot, ly=0: f'gamepad_test {slot} 0 {ly} 0 0 0 1'
        text = phase(['split_state', pad(0), pad(1), pad(2), 'wait 10', pad(1, 32767), pad(2, 32767),
                      'wait 40', pad(1), pad(2), 'wait 20', 'split_state',
                      pad(0, 32767), 'wait 40', pad(0), 'wait 20', 'split_state'])
        start, after_unused, after_one = snapshots(text)[-3:]
        assert all(moved(start[p], after_unused[p]) < 1 for p in (1, 2)), (start, after_unused)
        assert moved(after_unused[1], after_one[1]) > 20 and moved(after_unused[2], after_one[2]) < 1, (after_unused, after_one)
        def restart():
            # The round restart runs after the console buffer: wait until both profiles are reloaded.
            loads = log.read_text(encoding='latin-1').count('LOCALPROFILE loaded')
            phase(['map_restart 0 0'])
            deadline = time.monotonic() + 60
            while log.read_text(encoding='latin-1').count('LOCALPROFILE loaded') < loads + 2:
                assert time.monotonic() < deadline and client.poll() is None, 'Round restart did not reload profiles'
                time.sleep(.5)
            time.sleep(3)
        def xp_of(text):
            return dict((int(c), float(x)) for c, x in re.findall(r'LOCALPROFILE_TEST client=(\d+) .* xp=([\d.]+)', text))
        restart()  # Leave warmup: experience only counts while playing.
        text = phase([f'localprofile_test xp {one} 0 500', f'localprofile_test xp {two} 1 300', 'wait 5', 'localprofile_test'])
        xp = xp_of(text.split('localprofile_test xp')[-1])
        assert xp[one] == 500 and xp[two] == 300, xp
        restart()  # Objective mode clears XP on a restart; the profiles bring it back.
        xp = xp_of(phase(['localprofile_test']))
        assert xp[one] == 500 and xp[two] == 300, xp
        # Leaving saves; a new match restores the saved experience.
        text = phase(['disconnect', 'wait 120'] + match + ['localprofile_test'], 180)
        assert re.search(r'LOCALPROFILE loaded player=1 client=\d+ profile=alpha_one xp=500', text), text[-3000:]
        assert re.search(r'LOCALPROFILE loaded player=2 client=\d+ profile=ab xp=300', text), text[-3000:]
        saved = (mod / 'localprofiles/alpha_one.dat').read_text()
        assert 'name "Alpha One"' in saved and saved.split('xp ')[1].startswith('500.00'), saved
        console_command(client, 'quit')
        assert client.wait(timeout=30) == 0
        text = log.read_text(encoding='latin-1')
        assert not re.search(r'menu parse error|unknown (?:menu|item) keyword', text, re.I), log
        print(f'PASS player setup: pad-typed profile, separate keyboard/mouse mapped to player 2, controller 1 to player 1, profile names in game, '
              f'XP kept across round restart and matches, input routed to the mapped player only. {log}')
    finally:
        if client.poll() is None:
            client.terminate()
            client.wait(timeout=10)


if __name__ == '__main__':
    main()
