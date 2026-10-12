"""Team shell-base wheels: player 1 Axis (lacquered steel), player 2 Allies (brass), side by side.
python tests/team_wheels.py. Local loopback match only; screenshot build/team-wheels.png.
"""
import re
import subprocess

from PIL import Image
from play_game import prepare_game
from smoke_server import ROOT


def main():
    modules = ROOT / 'build/slice/Release'
    home = modules / 'smoke/team-wheels'
    command = prepare_game(modules, home)
    mod = home / 'et-rust'
    settings = dict(ui_rustMenuTest=1, developer=0, logfile=1, cl_splitScreen=1, cl_keyboardPlayer=0, ui_matchPlayers=0, g_matchPlayers=0,
                    omnibot_enable=0, bot_enable=0, g_gametype=2, g_doWarmup=0, g_warmup=0, com_maxfps=60, r_mode=-1,
                    r_customwidth=1280, r_customheight=720, r_customaspect=1280/720, r_fullscreen=0, r_swapInterval=0,
                    r_ignorehwgamma=1, s_initsound=0, in_gamepad=1, in_mouse=0, in_joystick=0, net_port=27983,
                    sv_pure=0, net_ip='127.0.0.1', sv_master1='', sv_master2='', sv_master3='', sv_master4='',
                    ui_oasisTeam1=1, ui_oasisClass1=2, ui_oasisWeapon1=0, in_gamepadIndex=0,
                    ui_oasisTeam2=2, ui_oasisClass2=2, ui_oasisWeapon2=0, in_gamepadIndex2=1)
    commands = ['devmap oasis', 'wait 300', 'local_join 1', 'local_join 2', 'wait 120', 'map_restart 0 0', 'wait 300']
    # Simulated pads (developer split-screen only): hold Y (equipment) on 1 and Up (requests) on 2.
    commands += ['set developer 1', 'gamepad_test 0 0 0 0 0 0 1', 'gamepad_test 1 0 0 0 0 0 1', 'wait 10',
                 'radial_test state 1', 'radial_test state 2',
                 'gamepad_test 0 0 0 0 0 32768 1', 'gamepad_test 1 0 0 0 0 1 1', 'wait 90',  # Hold delay + slide-in.
                 'radial_test state 1', 'radial_test state 2', 'screenshot team-wheels',
                 'set cl_gamepadRadialHelp 0', 'wait 10', 'screenshot team-wheels-nohelp', 'set cl_gamepadRadialHelp 1',
                 'gamepad_test 0 0 0 0 0 0 1', 'gamepad_test 1 0 0 0 0 0 1', 'wait 10', 'quit']
    (mod / 'team-wheels.cfg').write_text('\n'.join(commands) + '\n')
    (mod / 'autoexec.cfg').write_text((mod / 'autoexec.cfg').read_text() + '\n' +
                                      '\n'.join(f'set {k} "{v}"' for k, v in settings.items()) + '\n')
    result = subprocess.run(command + ['+exec', 'team-wheels.cfg'], cwd=home, timeout=300, capture_output=True)
    log = mod / 'etconsole.log'
    text = log.read_text(encoding='latin1')
    assert result.returncode == 0, (result.returncode, log)
    teams = re.findall(r'RADIAL_STATE health=\d+ pm=\d+ flags=\d+ team=(\d+)', text)
    assert teams[:2] == ['1', '2'], (teams, log)
    assert re.search(r'RADIAL_TEST player=1 kind=1', text) and re.search(r'RADIAL_TEST player=2 kind=2', text), log  # Open when captured.
    image = Image.open(mod / 'screenshots/team-wheels.tga').convert('RGB')
    image.save(ROOT / 'build/team-wheels.png')
    width, height = image.size
    # Wheel geometry (native/radial.c): 72% size, equipment on the left of its view, requests on the right.
    u = min(width / 2 / 640, height / 480); scale = u * .72; radius = 180 * scale

    def center(view):
        left = view * width // 2
        return (left + 28 * u + radius if view == 0 else left + width // 2 - 28 * u - radius), height / 2 - 8 * u

    def mean(img, x, y, size=6):
        pixels = [img.getpixel((int(x) + dx, int(y) + dy)) for dx in range(-size, size + 1) for dy in range(-size, size + 1)]
        return [sum(p[c] for p in pixels) / len(pixels) for c in range(3)]

    # Plain icon ring, left of each wheel's centre (between the stamped center and the icons).
    axis, allies = (mean(image, center(v)[0] - 80 * scale, center(v)[1]) for v in (0, 1))
    assert allies[0] > allies[2] + 30, ('Allied brass expected', allies)          # Warm yellow-brown.
    assert abs(axis[0] - axis[2]) < 25 and axis[1] >= axis[0] - 5, ('Axis steel expected', axis)  # Grey-green.
    # Center help off: only the dark overlay and captions go; the stamped shell center stays (brighter).
    bare = Image.open(mod / 'screenshots/team-wheels-nohelp.tga').convert('RGB')
    bare.save(ROOT / 'build/team-wheels-nohelp.png')
    for view, steel in ((0, True), (1, False)):
        cx, cy = center(view)
        spot = (cx - 30 * scale, cy + 30 * scale)  # Stamped area, clear of the primer and captions.
        on, off = mean(image, *spot, 4), mean(bare, *spot, 4)
        assert sum(off) > sum(on) * 1.15, ('Center overlay still drawn', view, on, off)
        assert (abs(off[0] - off[2]) < 30) if steel else (off[0] > off[2] + 25), ('Shell center missing', view, off)
        ring_on, ring_off = mean(image, cx - 80 * scale, cy), mean(bare, cx - 80 * scale, cy)
        assert max(abs(a - b) for a, b in zip(ring_on, ring_off)) < 12, ('Ring changed with center help off', view)
    print(f'PASS team wheels: Axis steel {axis}, Allied brass {allies}. build/team-wheels.png')


if __name__ == '__main__':
    main()
