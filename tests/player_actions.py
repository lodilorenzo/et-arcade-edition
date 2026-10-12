"""Two local players: weapon/ammo pickups, controller actions and mid-match team/class changes.
Run with the game closed: python tests/player_actions.py [--map oasis].
"""
import argparse
import re
import shutil
import subprocess
from smoke_server import ROOT, MAPS

# Stock bg_public.h weapon numbers and XInput button bits used below.
THOMPSON, MP40, PANZERFAUST, FG42 = 8, 3, 5, 33
UP, DOWN, LEFT, RIGHT, START, RB, A = 0x1, 0x2, 0x4, 0x8, 0x10, 0x200, 0x1000


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--map', choices=MAPS, default='oasis')
    args = parser.parse_args()
    modules = ROOT/'build/slice/Release'
    home = modules/f'smoke/player-actions-{args.map}'
    shutil.rmtree(home, ignore_errors=True)  # Only this test's isolated profile.
    mod = home/'et-rust'; mod.mkdir(parents=True)
    for name in ('qagame', 'cgame', 'cgame2', 'ui'):
        shutil.copy2(modules/f'{name}_mp_x86.dll', ROOT/'runtime/et-rust')
    shutil.copytree(ROOT/'native/ui', ROOT/'runtime/et-rust/ui', dirs_exist_ok=True)
    # Player one: Allied engineer (Thompson). Player two: Allied soldier (Thompson).
    settings = dict(ui_rustSlice=1, ui_rustMenuTest=1, ui_matchPlayers=0, ui_oasisMap=args.map,
                    ui_oasisTeam1=2, ui_oasisClass1=2, ui_oasisWeapon1=0,
                    ui_oasisTeam2=2, ui_oasisClass2=0, ui_oasisWeapon2=0,
                    cl_splitScreen=1, in_mouse=0, in_joystick=0, in_gamepad=1, in_gamepadIndex=0, in_gamepadIndex2=1, cl_keyboardPlayer=0,
                    developer=1, logfile=2, com_hunkMegs=256, sv_pure=0, net_ip="127.0.0.1", net_port=27981, bot_enable=0,
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='', cl_autoupdate=0, cl_punkbuster=0, sv_punkbuster=0,
                    r_mode=-1, r_customwidth=1280, r_customheight=720, r_customaspect=1280/720, r_fullscreen=0,
                    r_swapInterval=0, r_ignorehwgamma=1, com_maxfps=60, s_initsound=0, ui_TeamArenaFirstRun=1)
    (mod/'autoexec.cfg').write_text('\n'.join(f'set {k} "{v}"' for k, v in settings.items())+'\n')
    # As in omnibot.py: devmap keeps the developer-only fixtures, and a direct map_restart starts play
    # (a queued `wait` would otherwise hold back the countdown's own map_restart).
    commands = ['wait 100', 'ui_test script sliceStart', 'wait 600', f'devmap {args.map}', 'wait 300',
                'local_join 1', 'local_join 2', 'ui_test script closeingame',
                'map_restart 0 0', 'wait 300', 'set developer 1']

    def pad(slot, buttons=0, online=1):
        commands.append(f'gamepad_test {slot} 0 0 0 0 {buttons} {online}')

    def tap(slot, buttons, frames=4):
        pad(slot, buttons); commands.append(f'wait {frames}'); pad(slot); commands.append('wait 4')

    pad(0); pad(1)
    commands += ['wait 900', 'oasis_test player 0', 'oasis_test player 1']
    # Owned weapon at the feet: walking over it adds its ammunition.
    commands += ['oasis_test ammo 0 0', f'oasis_test drop 0 {THOMPSON}', 'wait 30', 'oasis_test player 0']
    # A different primary at the feet is not swapped automatically...
    commands += [f'oasis_test drop 0 {MP40}', 'wait 30', 'oasis_test player 0']
    # ...Activate on it swaps (player one keyboard/console, player two controller RB).
    commands += [f'oasis_test drop 0 {MP40} 1', 'wait 5', 'set cl_keyboardPlayer 1', '+activate', 'wait 5', '-activate', 'wait 30', 'set cl_keyboardPlayer 0', 'oasis_test player 0']
    commands += [f'oasis_test drop 1 {PANZERFAUST} 1', 'wait 5']
    tap(1, RB, 10); commands += ['wait 30', 'oasis_test player 1']
    # Controller prone (D-pad down toggles) and lean (D-pad right is held).
    tap(1, DOWN, 10); commands += ['wait 90', 'oasis_test player 1']
    tap(1, DOWN, 10); commands += ['wait 90']
    pad(1, RIGHT); commands += ['wait 40', 'oasis_test player 1']; pad(1); commands.append('wait 20')
    # Player one opens the match menu with Start: Team / Class -> Axis field ops -> Apply.
    tap(0, START); commands += ['wait 10', 'ui_test pad state']
    tap(0, DOWN); tap(0, A); commands += ['wait 10', 'loadout_test state']
    tap(0, RIGHT); tap(0, A); commands += ['loadout_test state']  # Field ops.
    tap(0, UP); tap(0, RIGHT); tap(0, A)  # Axis.
    tap(0, DOWN); tap(0, DOWN); tap(0, DOWN); commands += ['loadout_test state']
    tap(0, A); commands += ['wait 30', 'oasis_test player 0']
    # Player two's own Start: stay Allied, switch to covert ops with an FG42 for the next spawn.
    tap(1, START); commands += ['wait 10', 'ui_test pad state', 'cvarlist ui_menuPlayer']
    tap(1, DOWN); tap(1, A); commands += ['wait 10', 'loadout_test state']
    tap(1, LEFT); tap(1, A); commands += ['loadout_test state']  # Covert ops.
    tap(1, DOWN); tap(1, RIGHT); commands += ['loadout_test state']  # FG42.
    tap(1, DOWN); commands += ['loadout_test state']
    tap(1, A); commands += ['wait 30', 'oasis_test player 1']
    commands += ['disconnect', 'wait 100', 'quit']
    (mod/'actions-check.cfg').write_text('\n'.join(commands)+'\n')
    result = subprocess.run([str(modules/'et.exe'), '+set', 'fs_basepath', str(ROOT/'runtime'),
                             '+set', 'fs_homepath', str(home), '+set', 'fs_game', 'et-rust',
                             '+set', 'com_recommendedSet', '1', '+set', 'com_introPlayed', '1',
                             '+exec', 'actions-check.cfg'], cwd=ROOT/'runtime', capture_output=True, timeout=300)
    log = mod/'etconsole.log'
    text = log.read_text(encoding='latin1')
    assert result.returncode == 0 and '----- CL_Shutdown -----' in text, str(log)
    assert not re.search(r'ERROR:|menu parse error|reliable command overflow', text.replace('ERROR: Disconnected from server', ''), re.I), str(log)
    assert 'SPLITSCREEN ready players=0,1' in text, str(log)
    rows = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in re.findall(r'OASIS_TEST player ([^\n]+)', text)]
    p1 = [r for r in rows if r['client'] == '0']; p2 = [r for r in rows if r['client'] == '1']
    assert len(p1) == 5 and len(p2) == 5, (len(p1), len(p2), str(log))
    assert all(int(r['health']) > 0 for r in p1[:4] + p2[:3]), 'Players must have spawned before the action checks.'
    assert p1[0]['primary'] == str(THOMPSON) and p2[0]['primary'] == str(THOMPSON), (p1[0], p2[0])
    assert p1[1]['ammo'] == '30' and p1[1]['primary'] == str(THOMPSON), ('owned weapon must give ammo', p1[1])
    assert p1[2]['primary'] == str(THOMPSON), ('foreign weapon must not auto-swap', p1[2])
    assert p1[3]['primary'] == str(MP40), ('Activate must swap to the looked-at weapon', p1[3])
    assert p2[1]['primary'] == str(PANZERFAUST), ('player two RB must swap weapons', p2[1])
    assert p2[2]['prone'] == '1', ('D-pad down must go prone', p2[2])
    assert float(p2[3]['lean']) > 0, ('D-pad right must lean right', p2[3])
    assert p1[4]['team'] == '1' and p1[4]['latched'].startswith('3,'), ('player one team/class change', p1[4])
    assert p2[4]['team'] == '2' and p2[4]['latched'] == f'4,{FG42}', ('player two class/weapon change', p2[4])
    checks = re.findall(r'PAD_CHECK menu="([^"]*)" item="([^"]*)" cvar="([^"]*)" value="([^"]*)"', text)
    assert [c[:2] for c in checks] == [('ingame_main', 'RESUME'), ('ingame_main', 'RESUME')], checks
    panels = re.findall(r'LOADOUT_TEST player=(\d+) state=1 team=(\d+) class=(\d+) primary=(\d+)', text)
    assert len(panels) == 7 and [p[0] for p in panels] == ['1'] * 3 + ['2'] * 4, panels
    assert p2[4]['class'] == p2[0]['class'] and p2[4]['primary'] == str(PANZERFAUST), 'Class/weapon edits must wait for the next spawn'
    assert re.search(r'ui_menuPlayer[^\r\n]*"2"', text), 'Player two Start must own the menu.'
    print(f'PASS player actions on {args.map}: owned-weapon ammo, Activate/RB weapon swaps, prone/lean, '
          f'both players changing team/class/weapon mid-match through their own menus. {log}')


if __name__ == '__main__':
    main()
