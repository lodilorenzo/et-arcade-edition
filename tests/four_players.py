"""Four real local humans: simultaneous viewport menus and isolated gameplay; game closed."""
import re
import shutil
import subprocess
from PIL import Image, ImageChops, ImageStat
from smoke_server import ROOT


def main():
    modules = ROOT/'build/slice/Release'
    home = modules/'smoke/four-players'
    shutil.rmtree(home, ignore_errors=True)
    mod = home/'et-rust'; mod.mkdir(parents=True)
    for name in ('qagame', 'cgame', 'cgame2', 'cgame3', 'cgame4', 'ui'):
        shutil.copy2(modules/f'{name}_mp_x86.dll', ROOT/'runtime/et-rust')
    shutil.copytree(ROOT/'native/ui', ROOT/'runtime/et-rust/ui', dirs_exist_ok=True)
    # Avoid friendly-fire deaths while checking control isolation.
    settings = dict(ui_rustSlice=1, ui_rustMenuTest=1, ui_matchPlayers=0, cl_splitScreen=4, cl_keyboardPlayer=0,
                    in_mouse=0, in_joystick=0, in_gamepad=1, developer=1, logfile=2,
                    com_hunkMegs=256, g_friendlyFire=0, sv_pure=0, net_ip='127.0.0.1', net_port=27983,
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='', bot_enable=0,
                    cl_autoupdate=0, cl_punkbuster=0, sv_punkbuster=0, com_maxfps=60,
                    r_mode=-1, r_customwidth=1280, r_customheight=720, r_customaspect=1280/720,
                    r_fullscreen=0, r_swapInterval=0, r_ignorehwgamma=1, s_initsound=0,
                    ui_TeamArenaFirstRun=1)
    for i in range(1, 5):
        settings[f'ui_oasisTeam{i}'] = 2
        settings[f'ui_oasisClass{i}'] = 2
        settings[f'ui_oasisWeapon{i}'] = 0
        settings['in_gamepadIndex' + (str(i) if i > 1 else '')] = i - 1
    (mod/'autoexec.cfg').write_text('\n'.join(f'set {k} "{v}"' for k, v in settings.items())+'\n')
    commands = ['wait 100', 'ui_test script sliceStart', 'wait 300', 'devmap oasis',
                'wait 240', 'map_restart 0 0', 'wait 60', 'set developer 1',
                'set g_friendlyFire 0']

    def pad(slot, ly=0, rx=0, ry=0, buttons=0, online=1):
        commands.append(f'gamepad_test {slot} 0 {ly} {rx} {ry} {buttons} {online}')

    def tap(slot, button):
        pad(slot, buttons=button); commands.append('wait 4'); pad(slot); commands.append('wait 4')

    def states():
        commands.extend(f'loadout_test state {i}' for i in range(1, 5))

    for slot in range(4): pad(slot)
    commands += ['wait 10', 'screenshot four-menus']
    states()
    for slot, directions in ((1, (8,)), (2, (4,)), (3, (8, 8))):
        for direction in directions: tap(slot, direction)
        tap(slot, 4096)
    states()
    for button in (2, 2, 4096): tap(0, button)
    commands += ['wait 2400', 'split_state', 'screenshot four-one-playing']
    # Stock Controls pauses through the UI API; returning to a local class panel
    # must not pause the server when the other reserved humans are present.
    commands += ['ui_test script Controls', 'ui_test script sliceLoadout', 'wait 10',
                 'loadout_test key b 1', 'wait 10']
    pad(0, ly=32767); commands += ['wait 30']; pad(0)
    commands += ['wait 120', 'split_state', 'ui_test script closeingame']
    # One player's menu blocks only that player's keyboard/controller, not the world.
    commands += ['local_class 1', 'wait 10']
    pad(0, ly=32767, buttons=65536); commands += ['+forward', 'wait 30', '-forward']; pad(0)
    commands += ['wait 120', 'split_state', 'loadout_test key b 1', 'wait 10']
    for slot in range(1, 4):
        for button in (2, 2, 4096): tap(slot, button)
    commands += ['wait 2400', 'split_state', 'screenshot four-playing']
    for slot in range(4):
        # Aim above teammates: even harmless ET hits can interrupt friction on slopes.
        pad(slot, ry=32767); commands += ['wait 20']; pad(slot); commands += ['wait 4']
        # Player four backs away from teammates instead of exercising ET's physical player pushing.
        pad(slot, ly=-32767 if slot == 3 else 32767, rx=10000, buttons=65536); commands += ['wait 30']; pad(slot)
        commands += ['wait 120', 'split_state']
    # Reopen two different panels simultaneously using their own cgame commands.
    commands += ['split_cgame 3 openlimbomenu', 'split_cgame 4 openlimbomenu', 'ui_test defer four-finish.cfg']
    finish = ['wait 10', 'loadout_test state 3', 'loadout_test state 4',
              'screenshot four-reopened', 'split_state',
              'gamepad_test 0 0 32767 0 0 0 1', 'wait 30', 'gamepad_test 0 0 0 0 0 0 1',
              'wait 120', 'split_state', 'loadout_test key b 4', 'wait 10',
              'loadout_test state 3', 'split_state', 'set ui_oasisClass3 0', 'set ui_oasisClass4 0',
              'vid_restart', 'wait 2400', 'set developer 1', 'oasis_test player 2', 'oasis_test player 3',
              'split_state', 'disconnect', 'wait 100', 'quit']
    (mod/'four-finish.cfg').write_text('\n'.join(finish)+'\n')
    (mod/'four-check.cfg').write_text('\n'.join(commands)+'\n')
    result = subprocess.run([str(modules/'et.exe'), '+set', 'fs_basepath', str(ROOT/'runtime'),
                             '+set', 'fs_homepath', str(home), '+set', 'fs_game', 'et-rust',
                             '+set', 'com_recommendedSet', '1', '+set', 'com_introPlayed', '1',
                             '+exec', 'four-check.cfg'], cwd=ROOT/'runtime', capture_output=True, timeout=360)
    log = mod/'etconsole.log'; text = log.read_text(encoding='latin1')
    assert result.returncode == 0 and '----- CL_Shutdown -----' in text, str(log)
    assert not re.search(r'ERROR:|VM_Create on|reliable command overflow', text.replace('ERROR: Disconnected from server', ''), re.I), str(log)
    assert 'SPLITSCREEN ready players=0,1,2,3 ai2=0 layout=2x2' in text, str(log)
    panels = re.findall(r'LOADOUT_TEST player=(\d+) state=(\d+) team=(\d+) class=(\d+)', text)
    assert panels[:4] == [(str(i), '1', '2', '2') for i in range(1, 5)], panels
    assert panels[4:8] == [(str(i), '1', '2', str(c)) for i, c in enumerate((2, 3, 1, 4), 1)], panels
    assert panels[-4:] == [('3', '1', '2', '1'), ('4', '1', '2', '4'), ('4', '0', '2', '4'), ('3', '1', '2', '1')], panels
    rows = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in re.findall(r'SPLITPLAYER ([^\n]+)', text)]
    assert len(rows) == 48, (len(rows), str(log))
    frames = [rows[i:i+4] for i in range(0, len(rows), 4)]

    def distance(a, b):
        return max(abs(float(x)-float(y)) for x, y in zip(a.split(','), b.split(',')))

    for frame in frames:
        assert [r['player'] for r in frame] == ['1', '2', '3', '4'], frame
        assert [r['client'] for r in frame] == ['0', '1', '2', '3'], frame
        assert all(r['ai'] == '0' and int(r['cmd']) > 0 for r in frame), frame
        assert all(distance(r['pos'], r['predict']) < 12 for r in frame), frame
    assert [r['menu'] for r in frames[0]] == ['0', '1', '1', '1'], frames[0]
    assert distance(frames[1][0]['pos'], frames[0][0]['pos']) > 1
    assert all(distance(frames[1][i]['pos'], frames[0][i]['pos']) < .6 for i in range(1, 4))
    assert distance(frames[2][0]['pos'], frames[1][0]['pos']) < .6, 'Own menu must suppress movement'
    assert all(r['menu'] == '0' and int(r['health']) > 0 for r in frames[3]), frames[3]
    for slot in range(4):
        before, after = frames[3+slot], frames[4+slot]
        assert distance(before[slot]['pos'], after[slot]['pos']) > 1, (slot, before, after)
        assert int(after[slot]['clip']) < int(before[slot]['clip']), (slot, before, after)
        for other in range(4):
            if other == slot: continue
            assert distance(before[other]['pos'], after[other]['pos']) < .6, (slot, other, before, after)
            assert before[other]['clip'] == after[other]['clip'], (slot, other, before, after)
    assert [r['menu'] for r in frames[8]] == ['0', '0', '1', '1'], frames[8]
    assert distance(frames[9][0]['pos'], frames[8][0]['pos']) > 1, 'Other players must keep playing during mid-match selection'
    assert all(distance(frames[9][i]['pos'], frames[8][i]['pos']) < .6 for i in range(1, 4)), frames[8:10]
    assert [r['menu'] for r in frames[10]] == ['0', '0', '1', '0'], frames[10]
    assert re.findall(r'OASIS_TEST player client=(\d+) team=(\d+) class=(\d+)', text) == [('2', '2', '1'), ('3', '2', '4')], 'Reload must preserve confirmed classes'
    for name, locals_ in (('four-menus', range(4)), ('four-reopened', (2, 3))):
        image = Image.open(mod/f'screenshots/{name}.tga').convert('L')
        scale = min(640/640, 360/512); bias = (640 - 640*scale)/2
        for local in locals_:
            x, y = local % 2 * 640 + bias, local // 2 * 360
            ok = image.crop((int(x+454*scale), int(y+454*scale), int(x+536*scale), int(y+474*scale)))
            assert ok.getextrema()[1] - ok.getextrema()[0] > 50, (name, local)
    image = Image.open(mod/'screenshots/four-playing.tga').convert('RGB')
    views = [image.crop((i%2*640, i//2*360, i%2*640+640, i//2*360+360)) for i in range(4)]
    assert all(min(ImageStat.Stat(view).stddev) > 10 for view in views)
    assert all(sum(ImageStat.Stat(ImageChops.difference(views[0], view)).mean) > 10 for view in views[1:])
    print(f'PASS four local humans: 2x2 views, simultaneous independent stock menus, other players keep playing, isolated controls/ammo/prediction, reload and disconnect. {log}')


if __name__ == '__main__':
    main()
