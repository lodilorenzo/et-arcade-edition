"""Render the restyled unified menus and check the generated art loaded.
python tests/ui_art_check.py [--size 1280x720]. Opens a window; no network play.
Screenshots: build/ui-art-<menu>.png. Checks help docking and control clearance.
"""
import argparse
import re
import shutil
import subprocess

from PIL import Image
from legacy_modes import console_command
from online_check import wait_log
from play_game import prepare_game
from smoke_server import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--size', default='1280x720')
    args = parser.parse_args()
    width, height = map(int, args.size.split('x'))
    modules = ROOT / 'build/slice/Release'
    home = modules / 'smoke/ui-art'
    shutil.rmtree(home, ignore_errors=True)  # Only this check's private profile.
    command = prepare_game(modules, home)
    mod = home / 'et-rust'
    settings = dict(r_mode=-1, r_customwidth=width, r_customheight=height, r_customaspect=width / height, r_fullscreen=0,
                    r_swapInterval=0, r_ignorehwgamma=1, in_mouse=0, in_gamepad=0, s_initsound=0, logfile=2, developer=1,
                    ui_rustMenuTest=1, ui_showtooltips=1, com_maxfps=60, cl_master='127.0.0.1:9', cl_splitScreen=0)
    with (mod / 'autoexec.cfg').open('a') as config:
        config.write('\n'.join(f'set {k} "{v}"' for k, v in settings.items()) + '\n')
    shots = ['main', 'main-focus', 'browser', 'ingame', 'options', 'gamepad', 'radial', 'players', 'maps']
    steps = ['wait 90', 'ui_test mouse -640 -480', 'wait 10', 'screenshot ui-art-main',
             'ui_test pad down', 'ui_test pad down', 'ui_test pad state', 'wait 10', 'screenshot ui-art-main-focus',
             'ui_test script sliceBrowse', 'wait 60', 'ui_test pad right', 'wait 10', 'screenshot ui-art-browser']
    (mod / 'ui-art.cfg').write_text('\n'.join(steps + ['set UI_ART_MAIN_DONE 1', 'UI_ART_MAIN_DONE']) + '\n')
    # ui_test <menu> opens one menu alone (and reports its item count).
    (mod / 'ui-art-options.cfg').write_text('\n'.join([
        'ui_test main', 'ui_test playonline', 'ui_test ingame_main', 'wait 20', 'screenshot ui-art-ingame',
        'ui_test options_system', 'ui_test pad down', 'ui_test pad state', 'wait 40', 'screenshot ui-art-options',
        'ui_test options_gamepad', 'ui_test pad state', 'wait 40', 'screenshot ui-art-gamepad',
        'ui_test options_radial', 'ui_test pad state', 'wait 40', 'screenshot ui-art-radial',
        'ui_test players_setup', 'ui_test pad state', 'wait 40', 'screenshot ui-art-players',
        'ui_test slice_maps', 'wait 40', 'screenshot ui-art-maps',
        'set UI_ART_OPTIONS_DONE 1', 'UI_ART_OPTIONS_DONE']) + '\n')
    log = mod / 'etconsole.log'
    client = subprocess.Popen(command + ['+exec', 'ui-art.cfg'], cwd=home, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        wait_log(log, 'UI_ART_MAIN_DONE', client)
        console_command(client, 'exec ui-art-options.cfg')
        wait_log(log, 'UI_ART_OPTIONS_DONE', client)
        console_command(client, 'quit')
        assert client.wait(timeout=20) == 0
        text = log.read_text(encoding='latin-1')
        counts = {name: int(n) for name, n in re.findall(r'UI test menu: (\S+) items=(\d+)', text)}
        assert len(counts) == 8 and max(counts.values()) < 128, counts  # ET's MAX_MENUITEMS.
        clearances = re.findall(r'SHELL_CHECK menu=(\S+) helpOverlaps=(\d+)', text)
        assert len(clearances) == 8 and all(n == '0' for _, n in clearances), clearances
        tips = re.findall(r'HELP_CHECK menu="([^"]+)" docked=(\d+)', text)
        assert {'main', 'options_system', 'options_gamepad', 'options_radial', 'players_setup'} <= {m for m, _ in tips}, tips
        assert all(docked == '1' for _, docked in tips), tips
        print('menu items:', counts)
        problems = re.findall(r'.*(?:menu parse error|unknown (?:menu|item) keyword|Couldn\'t find image for shader ui/art|MAX_MENUITEMS|too many).*', text, re.I)
        assert not problems, problems
        for name in shots:
            path = mod / f'screenshots/ui-art-{name}.tga'
            image = Image.open(path)
            assert image.size == (width, height), (name, image.size)
            image.save(ROOT / f'build/ui-art-{name}.png')
            if name == 'options':
                # A centered hint must remain a horizontal line, not wrap one letter per line.
                scale = min(width / 640, height / 480)
                ox, oy = (width - 640 * scale) / 2, (height - 480 * scale) / 2
                box = tuple(round(v) for v in (ox + 114 * scale, oy + 68 * scale, ox + 526 * scale, oy + 78 * scale))
                pixels = list(image.convert('RGB').crop(box).getdata())
                assert sum(min(p) > 150 for p in pixels) > 100 * scale * scale, 'Centered hint lost its usable wrap width'
        print(f'PASS ui art: 8 menus, art loaded, focused help docked and clear of controls at {width}x{height}; screenshots build/ui-art-*.png. {log}')
    finally:
        if client.poll() is None:
            client.terminate(); client.wait(timeout=10)


if __name__ == '__main__':
    main()
