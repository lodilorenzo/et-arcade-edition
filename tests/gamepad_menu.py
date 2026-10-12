"""Actual native UI gamepad-key regression (not direct menu-action scripts).
Run with the game closed: python tests/gamepad_menu.py [--width 640 --height 480].
XInput/stick/repeat/menu-transition coverage is in the runnable gamepad_checks CTest.
"""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
from PIL import Image
from smoke_server import ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--width', type=int, default=640)
    parser.add_argument('--height', type=int, default=480)
    args = parser.parse_args()
    modules = ROOT/'build/slice/Release'
    home = modules/f'smoke/gamepad-menu-{args.width}'
    mod = home/'et-rust'
    shutil.rmtree(home, ignore_errors=True)  # This test owns its isolated smoke profile.
    mod.mkdir(parents=True, exist_ok=True)
    for name in ('qagame', 'cgame', 'ui'):
        shutil.copy2(modules/f'{name}_mp_x86.dll', ROOT/'runtime/et-rust')
    shutil.copytree(ROOT/'native/ui', ROOT/'runtime/et-rust/ui', dirs_exist_ok=True)
    settings = dict(ui_matchPlayers=8, ui_rustSlice=1, ui_rustMenuTest=1, bot_enable=0,
                    g_gametype=2, g_doWarmup=0, g_warmup=0, g_timelimit=0,
                    sv_maxclients=8, sv_pure=0, net_ip='127.0.0.1', net_port=27978,
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='',
                    cl_autoupdate=0, cl_punkbuster=0, sv_punkbuster=0,
                    com_hunkMegs=256, logfile=2, developer=1, com_maxfps=60,
                    r_mode=-1, r_customwidth=args.width, r_customheight=args.height,
                    r_customaspect=args.width/args.height, r_fullscreen=0,
                    r_displayRefresh=0, r_swapInterval=0, r_ignorehwgamma=1,
                    s_initsound=1, s_volume=.7, s_musicvolume=.25,
                    ui_rustMonitorWidth=args.width, ui_rustMonitorHeight=args.height,
                    ui_rustMonitorRefresh=0, ui_rustDisplayModesTest='1920x1080 1280x720 640x480', cl_gamepadYawSpeed=120, cl_gamepadInvert=0,
                    cl_gamepadFriction=.65, cl_gamepadAdhesion=.45,
                    cl_gamepadMoveDeadzone=.2, cl_gamepadLookDeadzone=.2,
                    cl_gamepadPitchSpeed=90, cl_gamepadAccelScale=2,
                    in_mouse=0, in_joystick=0, in_gamepad=1, in_gamepadIndex=3, in_gamepadIndex2=2, cl_splitScreen=0, cg_gamepadAimAssist=1, ui_TeamArenaFirstRun=1)
    (mod/'autoexec.cfg').write_text('\n'.join(f'set {k} "{v}"' for k, v in settings.items())+'\n')
    commands = ['wait 60']
    phases = []

    def next_phase(action=None):
        filename = f'pad-phase-{len(phases)}.cfg'
        commands.append(f'ui_test defer {filename}')
        if action:
            key(action)  # Queue continuation while the UI VM still exists, then perform the action.
        (mod/('pad-check.cfg' if not phases else phases[-1])).write_text('\n'.join(commands)+'\n')
        phases.append(filename)
        commands.clear()
    expected = []

    def key(name, count=1):
        commands.extend([f'ui_test pad {name}']*count)

    def check(menu, item='', cvar='', value=None, editing=0):
        # Compare explicit state checkpoints, not the unasserted event trace.
        commands.append('ui_test pad state')
        expected.append((menu, item, cvar, value, editing))

    # Resolution selector: the fixture's monitor modes, plus the current size when it is not listed.
    modes = ['1920x1080', '1280x720', '640x480']
    start = f'{args.width}x{args.height}'
    if start not in modes:
        modes.append(start)

    def steps(source, target):
        return (modes.index(target) - modes.index(source)) % len(modes)

    def number(value):
        # Native numeric editor: clear to one digit, then set/append every desired digit.
        key('a')
        key('x', 5)
        for i, digit in enumerate(str(value)):
            if i:
                key('right')
                key('up')  # append a 1 (or edit the existing first digit)
                key('down')  # back to 0
            key('up', int(digit))
        key('a')

    check('main', 'START MATCH')
    key('down'); check('main', 'Players:', 'cl_splitScreen', '0')
    key('right'); check('main', 'Players:', 'cl_splitScreen', '1')
    key('left'); check('main', 'Players:', 'cl_splitScreen', '0')
    key('a'); check('main', 'Players:', 'cl_splitScreen', '1')
    key('right'); check('main', 'Players:', 'cl_splitScreen', '3')
    key('right'); check('main', 'Players:', 'cl_splitScreen', '4')
    key('right'); check('main', 'Players:', 'cl_splitScreen', '0')  # wrap
    key('left'); check('main', 'Players:', 'cl_splitScreen', '4')
    key('left'); check('main', 'Players:', 'cl_splitScreen', '3')
    key('left'); check('main', 'Players:', 'cl_splitScreen', '1')
    key('left'); check('main', 'Players:', 'cl_splitScreen', '0')
    # Map opens the chooser (tests/map_list.py); B returns here with the map unchanged.
    key('down'); check('main', 'Map:')
    key('a'); check('slice_maps', '', 'ui_rustMapPick', 'oasis')
    key('down'); key('b'); check('main', 'START MATCH')
    key('down', 2); check('main', 'Map:')
    key('down'); check('main', 'Total players:', 'ui_matchPlayers', '8')
    for count in (12, 16, 24, 32, 40, 48):
        key('right'); check('main', 'Total players:', 'ui_matchPlayers', str(count))
    key('right'); check('main', 'Total players:', 'ui_matchPlayers', '4')
    key('a'); check('main', 'Total players:', 'ui_matchPlayers', '8')
    # Players & Controllers (tests/players_setup.py covers it fully): opens on player 1, B returns.
    key('down'); check('main', 'PLAYERS & CONTROLLERS')
    key('a'); check('players_setup', 'Profile:', 'ui_localProfile1', '')
    key('down'); check('players_setup', 'Controller:', 'in_gamepadIndex', '3')
    key('b'); check('main', 'START MATCH')
    key('down', 4); check('main', 'PLAYERS & CONTROLLERS')
    key('down'); check('main', 'OPTIONS')
    key('down'); check('main', 'QUIT')
    key('down'); check('main', 'START MATCH')
    key('up'); check('main', 'QUIT')
    key('up'); key('a'); check('options_system', 'USE MONITOR RESOLUTION / REFRESH')
    key('down'); check('options_system', 'Resolution:', 'ui_rustResolution', start)
    key('left'); check('options_system', 'Resolution:', 'ui_rustResolution', modes[(modes.index(start) - 1) % len(modes)])
    key('right'); check('options_system', 'Resolution:', 'ui_rustResolution', start)  # wraps both ways
    if steps(start, '1280x720'):
        key('right', steps(start, '1280x720'))
    check('options_system', 'Resolution:', 'ui_rustResolution', '1280x720')
    key('down'); key('right'); check('options_system', 'Fullscreen:', 'ui_r_fullscreen', '1')
    key('left'); key('down'); key('right'); check('options_system', 'Refresh:', 'ui_r_displayRefresh', '60')
    key('left'); key('down'); key('a'); check('options_system', 'V-Sync:', 'ui_r_swapInterval', '1')
    key('a'); key('down'); key('right'); check('options_system', 'FPS cap (0 = off):', 'ui_com_maxfps', '61')
    number(144); check('options_system', 'FPS cap (0 = off):', 'ui_com_maxfps', '144')
    key('a'); check('options_system', 'FPS cap (0 = off):', 'ui_com_maxfps', '144', editing=1)
    key('up'); key('x'); check('options_system', 'FPS cap (0 = off):', 'ui_com_maxfps', '44', editing=1)
    key('b')
    check('options_system', 'FPS cap (0 = off):', 'ui_com_maxfps', '144')  # cancellation restores exact original
    key('left'); key('down'); key('right'); key('left')  # texture detail choices
    key('down'); key('left'); key('right')  # string choices
    key('down'); key('right'); key('left')  # anisotropy toggle
    key('down'); key('right'); key('left')  # filtering level choices
    key('down'); key('right'); key('a')
    check('options_system', 'Effects volume:', 's_volume', .72)  # A must not jump a slider to the mouse
    key('left'); key('down'); key('right'); check('options_system', 'Music volume:', 's_musicvolume', .27)
    key('left'); key('down'); key('right'); key('left')  # mouse sensitivity
    key('down'); key('a'); check('options_gamepad', 'Enable XInput controller:', 'in_gamepad', '1')
    key('right'); check('options_gamepad', 'Enable XInput controller:', 'in_gamepad', '0')
    key('left')  # can re-enable without keyboard/mouse
    for label, cvar, value in [('Move stick deadzone:', 'cl_gamepadMoveDeadzone', .21),
                               ('Look stick deadzone:', 'cl_gamepadLookDeadzone', .21),
                               ('Horizontal look speed:', 'cl_gamepadYawSpeed', 126.6),
                               ('Vertical look speed:', 'cl_gamepadPitchSpeed', 94.2)]:
        key('down'); key('right'); check('options_gamepad', label, cvar, value); key('left')
    key('down'); key('a'); check('options_gamepad', 'Invert vertical look:', 'cl_gamepadInvert', '1')
    key('down'); key('right'); key('left')  # acceleration
    key('down'); key('a'); check('options_gamepad', 'Aiming assistance:', 'cg_gamepadAimAssist', '0')
    key('a'); key('down'); key('right'); check('options_gamepad', 'Aim assist strength:', 'cl_gamepadAssistStrength', '1.3')
    key('left'); key('left'); check('options_gamepad', 'Aim assist strength:', 'cl_gamepadAssistStrength', '0.7')
    key('right')  # back to Normal
    key('down'); key('right'); key('left')  # friction
    key('down'); key('right'); key('left')  # adhesion
    key('down'); check('options_gamepad', 'BACK')
    key('a'); check('options_system', 'GAMEPAD SETTINGS')
    key('up', 12); check('options_system', 'Resolution:', 'ui_rustResolution', '1280x720')  # pending choice survived modal return
    key('down', 12); key('a'); key('b'); check('options_system', 'GAMEPAD SETTINGS')
    key('down'); check('options_system', 'BACK')
    key('right'); check('options_system', 'APPLY')
    key('a'); commands.append('wait 120'); check('vid_confirm', 'YES')
    key('right'); check('vid_confirm', 'NO')
    key('left'); key('a'); check('main', 'START MATCH')
    commands.append('screenshot pad-confirmed')
    key('down', 5); key('a'); key('down'); check('options_system', 'Resolution:', 'ui_rustResolution', '1280x720')
    if steps('1280x720', start):
        key('right', steps('1280x720', start))
    check('options_system', 'Resolution:', 'ui_rustResolution', start); key('down', 14)
    check('options_system', 'APPLY'); key('a'); commands.append('wait 120'); key('b'); next_phase(); commands.append('wait 120')
    check('main', 'START MATCH')  # reverting keeps the previously accepted 1280x720
    next_phase('a'); commands += ['wait 1000', 'set developer 1']
    # The stock cgame limbo panel replaces the custom class picker.
    commands += ['loadout_test state', 'loadout_test key down', 'loadout_test key down',
                 'loadout_test key a', 'wait 20', 'openlimbomenu']
    next_phase(); commands += ['wait 20', 'loadout_test state', 'loadout_test key b', 'wait 5']
    key('start'); check('ingame_main', 'RESUME')
    key('down', 2); key('a'); check('options_system', 'USE MONITOR RESOLUTION / REFRESH')
    key('b'); check('ingame_main', 'RESUME')
    key('down', 3); check('ingame_main', 'LEAVE MATCH'); next_phase('a'); commands.append('wait 100')
    check('main', 'START MATCH')
    commands += ['writeconfig pad-result.cfg', 'screenshot pad-returned']
    key('down', 6); check('main', 'QUIT'); key('a')  # real menu action, not a console quit
    (mod/phases[-1]).write_text('\n'.join(commands)+'\n')
    log = mod/'etconsole.log'; log.unlink(missing_ok=True)
    result = subprocess.run([str(modules/'et.exe'), '+set', 'fs_basepath', str(ROOT/'runtime'),
                             '+set', 'fs_homepath', str(home), '+set', 'fs_game', 'et-rust',
                             '+set', 'com_recommendedSet', '1', '+set', 'com_introPlayed', '1',
                             '+exec', 'pad-check.cfg'], cwd=ROOT/'runtime', capture_output=True, timeout=240)
    text = log.read_text(encoding='latin1')
    assert result.returncode == 0 and '----- CL_Shutdown -----' in text, str(log)
    assert not re.search(r'ERROR:|menu parse error|modal window with an empty', text, re.I), str(log)
    rows = re.findall(r'PAD_CHECK menu="([^"]*)" item="([^"]*)" cvar="([^"]*)" value="([^"]*)" editing=(\d)', text)
    assert len(rows) == len(expected), (len(rows), len(expected), str(log))
    for i, (actual, wanted) in enumerate(zip(rows, expected)):
        menu, item, cvar, value, editing = wanted
        assert actual[:2] == (menu, item) and actual[2].lower() == cvar.lower() and int(actual[4]) == editing, (i, actual, wanted, str(log))
        if isinstance(value, str) and not value.replace('.', '', 1).isdigit():
            assert actual[3] == value, (i, actual, wanted)
        elif value is not None:
            assert abs(float(actual[3]) - float(value)) < .001, (i, actual, wanted)
    assert Image.open(mod/'screenshots/pad-confirmed.tga').size == (1280, 720)
    assert Image.open(mod/'screenshots/pad-returned.tga').size == (1280, 720)
    saved = (mod/'pad-result.cfg').read_text(encoding='latin1')
    assert 'seta cl_gamepadInvert "1"' in saved and 'seta cg_gamepadAimAssist "1"' in saved and 'seta cl_gamepadAssistStrength "1"' in saved
    assert 'LOADOUT confirmed player=1' in text, 'Start Match did not confirm the stock loadout.'
    assert text.count('LOADOUT opened player=1 stock_limbo=1') == 2, 'Initial selection and L must use the original limbo panel.'
    assert 'SPLITSCREEN ready' not in text, 'Selecting one player must start an ordinary match.'
    print(f'PASS controller native menus: {len(expected)} focus/value checkpoints, every settings type, digits/cancel, modal preservation, video keep/revert, Start/Resume/Leave/Quit at {args.width}x{args.height}. {log}')


if __name__ == '__main__':
    main()
