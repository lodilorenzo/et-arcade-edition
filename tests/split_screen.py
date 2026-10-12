"""Native two-player Oasis regression; run with the game closed."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
from PIL import Image, ImageChops, ImageStat
from smoke_server import ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    args = parser.parse_args()
    modules = ROOT/'build/slice/Release'
    home = modules/f'smoke/split-{args.width}'
    shutil.rmtree(home, ignore_errors=True)  # Only this test's isolated profile.
    mod = home/'et-rust'; mod.mkdir(parents=True)
    for name in ('qagame', 'cgame', 'cgame2', 'ui'):
        shutil.copy2(modules/f'{name}_mp_x86.dll', ROOT/'runtime/et-rust')
    shutil.copytree(ROOT/'native/ui', ROOT/'runtime/et-rust/ui', dirs_exist_ok=True)
    settings = dict(ui_matchPlayers=0, ui_rustSlice=1, ui_rustMenuTest=1, cl_splitScreen=0, in_mouse=0,
                    in_gamepad=1, in_gamepadIndex=0, in_gamepadIndex2=1, cl_keyboardPlayer=0,
                    cg_gamepadAimAssist=1, cl_gamepadInvert=0, bot_enable=0,
                    g_gametype=2, g_doWarmup=0, g_warmup=0, g_timelimit=0,
                    sv_maxclients=8, sv_pure=0, net_ip='127.0.0.1', net_port=27979,
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='',
                    cl_autoupdate=0, cl_punkbuster=0, sv_punkbuster=0,
                    com_hunkMegs=256, logfile=2, developer=1, com_maxfps=60,
                    r_mode=-1, r_customwidth=args.width, r_customheight=args.height,
                    r_customaspect=args.width/args.height, r_fullscreen=0,
                    r_displayRefresh=0, r_swapInterval=0, r_ignorehwgamma=1,
                    s_initsound=1, ui_TeamArenaFirstRun=1)
    (mod/'autoexec.cfg').write_text('\n'.join(f'set {k} "{v}"' for k, v in settings.items())+'\n')
    commands = ['wait 100', 'ui_test pad down', 'ui_test pad right', 'ui_test pad state',
                'ui_test pad up', 'ui_test pad a', 'wait 240', 'devmap oasis', 'wait 240']  # Keep developer-only input fixtures enabled through the countdown restart.

    def pad(slot, lx=0, ly=0, rx=0, ry=0, buttons=0, online=1):
        commands.append(f'gamepad_test {slot} {lx} {ly} {rx} {ry} {buttons} {online}')

    pad(0); pad(1); commands += ['wait 10', 'loadout_test state 1', 'loadout_test state 2', 'oasis_test player 0', 'oasis_test player 1',
                               'screenshot class-player1']
    # Player two can browse simultaneously without changing player one's panel.
    pad(1, buttons=8); commands += ['wait 4']; pad(1); commands += ['wait 4', 'loadout_test state 1']
    for button in (2, 2, 4096):
        pad(0, buttons=button); commands += ['wait 4']; pad(0); commands += ['wait 4']
    commands += ['wait 10', 'loadout_test state 2', 'screenshot class-player2']
    # Player one's gameplay input must not affect player two's still-open panel.
    pad(0, buttons=4096); commands += ['wait 4']; pad(0); commands += ['wait 4', 'loadout_test state 2']
    for button in (2, 8, 2, 4096):  # Engineer rifle, then OK.
        pad(1, buttons=button); commands += ['wait 4']; pad(1); commands += ['wait 4']
    # Execute the countdown restart directly; cfg waits hold back its queued command.
    commands += ['map_restart 0 0', 'wait 60', 'set developer 1', 'wait 5',
                 'ui_test pad state', 'split_state', 'screenshot split-idle']
    pad(1, lx=32767, rx=18000, buttons=65536)
    commands += ['wait 30']; pad(1); commands += ['wait 120', 'split_state', 'screenshot split-p2']  # Allow friction/snapshots to settle before isolation checks.
    pad(0, ly=32767, rx=-18000)
    commands += ['wait 30']; pad(0); commands += ['wait 120', 'split_state', 'screenshot split-p1']
    commands += ['set cl_keyboardPlayer 1', '+forward', 'wait 20', '-forward', 'wait 10', 'split_state', 'set cl_keyboardPlayer 0']
    pad(1, online=0); commands += ['wait 10', 'split_state', 'screenshot split-unplug']
    pad(1, buttons=65536); commands += ['wait 10', 'split_state']  # held trigger must be suppressed on reconnect
    pad(1); commands += ['wait 4']
    pad(1, buttons=16); commands += ['wait 3']; pad(1); commands += ['wait 3', 'ui_test pad state']
    pad(1, buttons=2); commands += ['wait 3']; pad(1); commands += ['wait 3']  # Team / Class
    pad(1, buttons=2); commands += ['wait 3']; pad(1); commands += ['wait 3']  # Options
    pad(1, buttons=4096); commands += ['wait 3']; pad(1); commands += ['wait 3', 'ui_test pad state']
    pad(1, buttons=8192); commands += ['wait 3']; pad(1); commands += ['wait 3', 'ui_test pad state']
    pad(1, buttons=4096); commands += ['wait 3']; pad(1); commands += ['wait 10', 'split_state', 'screenshot split-resumed']
    # Video reloads reconnect player two with the confirmed loadout, never a draft edit.
    commands += ['set ui_oasisClass2 1', 'vid_restart', 'wait 240', 'set developer 1', 'wait 5',
                 'ui_test pad state', 'oasis_test player 1', 'screenshot split-reloaded',
                 'split_cgame openlimbomenu', 'ui_test defer split-finish.cfg']
    # Cgame appends L's engine command; queue continuation after it, not before it.
    (mod/'split-finish.cfg').write_text('wait 10\nloadout_test state 2\nloadout_test key b 2\nwait 5\ndisconnect\nwait 120\nui_test pad state\nscreenshot split-main\nquit\n')
    (mod/'split-check.cfg').write_text('\n'.join(commands)+'\n')
    log = mod/'etconsole.log'
    result = subprocess.run([str(modules/'et.exe'), '+set', 'fs_basepath', str(ROOT/'runtime'),
                             '+set', 'fs_homepath', str(home), '+set', 'fs_game', 'et-rust',
                             '+set', 'com_recommendedSet', '1', '+set', 'com_introPlayed', '1',
                             '+exec', 'split-check.cfg'], cwd=ROOT/'runtime', capture_output=True, timeout=160)
    text = log.read_text(encoding='latin1')
    assert result.returncode == 0 and '----- CL_Shutdown -----' in text, str(log)
    assert not re.search(r'ERROR:|VM_Create on|reliable command overflow|modal window with an empty', text.replace('ERROR: Disconnected from server', ''), re.I), str(log)
    assert 'SPLITSCREEN ready players=0,1 ai2=0 layout=side-by-side' in text, str(log)
    pattern = r'SPLITSTATE ids=(\d+),(\d+) ai2=(\d+) cmd=(\d+),(\d+) health=(-?\d+),(-?\d+) clip=(\d+),(\d+) generation=(\d+),(\d+) pos1=([^ ]+) pos2=([^ ]+) predict1=([^ ]+) predict2=([^ ]+) yaw=([^\n]+)'
    rows = re.findall(pattern, text)
    assert len(rows) == 7, (len(rows), str(log))
    def distance(a, b):
        return max(abs(float(x) - float(y)) for x, y in zip(a.split(','), b.split(',')))

    for row in rows:
        assert row[:3] == ('0', '1', '0'), row
        assert int(row[3]) > 0 and int(row[4]) > 0, row
        for actual, predicted in ((row[11], row[13]), (row[12], row[14])):
            assert distance(actual, predicted) < 12, row
    assert int(rows[1][8]) < int(rows[0][8]) and rows[1][7] == rows[0][7], rows[:2]  # player-two fire cannot spend player-one ammo
    assert distance(rows[1][11], rows[0][11]) < .25 and distance(rows[1][12], rows[0][12]) > 1, rows[:2]
    assert distance(rows[2][11], rows[1][11]) > 1 and distance(rows[2][12], rows[1][12]) < .25, rows[1:3]
    assert distance(rows[3][11], rows[2][11]) > 1 and distance(rows[3][12], rows[2][12]) < .25, rows[2:4]  # keyboard only belongs to player one
    assert distance(rows[6][12], rows[5][12]) < .25, rows[5:7]  # accepting Resume must not jump in that same frame
    assert rows[5][8] == rows[4][8], rows[4:6]  # reconnect-held fire suppressed
    checkpoints = re.findall(r'PAD_CHECK menu="([^"]*)" item="([^"]*)"', text)
    assert checkpoints == [('main', 'Players:'), ('', ''),
                           ('ingame_main', 'RESUME'), ('options_system', 'USE MONITOR RESOLUTION / REFRESH'),
                           ('ingame_main', 'RESUME'), ('', ''), ('main', 'START MATCH')], checkpoints
    players = re.findall(r'OASIS_TEST player client=(\d+) team=(\d+) class=(\d+)', text)
    assert [p[:2] for p in players[:2]] == [('0', '3'), ('1', '3')], 'Both humans must choose before joining'
    assert players[2:] == [('1', '2', '2')], 'Video reload must preserve the confirmed class, not apply a draft'
    assert text.count('LOADOUT confirmed player=1') == text.count('LOADOUT confirmed player=2') == 1, 'No automatic joins or repeated prompts'
    panels = re.findall(r'LOADOUT_TEST player=(\d+) state=(\d+) team=(\d+) class=(\d+) primary=(\d+) secondary=(\d+) row=(\d+) column=(\d+)', text)
    assert len(panels) == 7 and panels[0] == panels[2] and panels[3] == panels[4], ('Other controllers must not operate the panel', panels)
    assert [p[0] for p in panels[:5]] == ['1', '2', '1', '2', '2'], ('Both stock panels must be independently available', panels)
    assert panels[5][:5] == ('2', '1', '2', '2', '24') and panels[6][:2] == ('2', '0'), ('Reopening after video reload must preserve the confirmed engineer rifle, not the draft medic class', panels)
    # Each original panel must fit its viewport, including the bottom OK button.
    for name, local in (('class-player1', 0), ('class-player1', 1), ('class-player2', 1)):
        panel = Image.open(mod/f'screenshots/{name}.tga').convert('L')
        width = args.width // 2
        scale = min(width / 640, args.height / 512)
        bias = width * local + (width - 640 * scale) / 2
        ok = panel.crop((int(bias + 454 * scale), int(454 * scale), int(bias + 536 * scale), int(474 * scale)))
        assert ok.getextrema()[1] - ok.getextrema()[0] > 50, ('Stock limbo must fit its own viewport', name, local)
    image = Image.open(mod/'screenshots/split-p2.tga').convert('RGB')
    assert image.size == (args.width, args.height)
    half = args.width//2
    left = image.crop((0, 0, half, args.height)); right = image.crop((half, 0, half*2, args.height))
    assert min(ImageStat.Stat(left).stddev) > 10 and min(ImageStat.Stat(right).stddev) > 10
    assert sum(ImageStat.Stat(ImageChops.difference(left, right)).mean) > 10, 'Views must be independent, not duplicated.'
    scale = min(half/640, args.height/480)
    for view in (left, right):
        status = view.crop((0, int(args.height-100*scale), int(150*scale), args.height))
        assert any(g > 70 and g > r*1.4 and g > b*1.4 for r, g, b in status.getdata()), 'Health HUD must anchor to each viewport bottom.'
    assert re.search(r'cvar="cl_splitScreen" value="1"', text, re.I)
    print(f'PASS native split-screen {args.width}x{args.height}: menu Start Match, two human identities/prediction, isolated controllers/ammo/keyboard, hot-plug, real controller menus/resume, distinct rendered views and clean disconnect. {log}')


if __name__ == '__main__':
    main()
