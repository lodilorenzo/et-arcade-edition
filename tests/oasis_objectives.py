"""Native original Oasis + local controllers; run with the game closed."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
from PIL import Image
from smoke_server import ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--players', type=int, choices=(1, 2), default=2)
    parser.add_argument('--winner', choices=('allies', 'axis'), default='allies')
    args = parser.parse_args()
    modules = ROOT/'build/slice/Release'
    home = modules/f'smoke/oasis-objectives-{args.players}-{args.winner}'
    shutil.rmtree(home, ignore_errors=True)
    mod = home/'et-rust'; mod.mkdir(parents=True)
    for name in ('qagame', 'cgame', 'cgame2', 'ui'):
        shutil.copy2(modules/f'{name}_mp_x86.dll', ROOT/'runtime/et-rust')
    shutil.copytree(ROOT/'native/ui', ROOT/'runtime/et-rust/ui', dirs_exist_ok=True)
    settings = dict(ui_rustSlice=1, ui_rustMenuTest=1, ui_matchPlayers=0, in_mouse=0,
                    ui_oasisTeam1=2, ui_oasisTeam2=2, ui_oasisClass1=2, ui_oasisClass2=2,
                    cl_splitScreen=0, developer=1, logfile=2, com_hunkMegs=256,
                    sv_pure=0, net_ip='127.0.0.1', net_port=27976, bot_enable=0,
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='',
                    cl_autoupdate=0, cl_punkbuster=0, sv_punkbuster=0,
                    r_mode=-1, r_customwidth=1280, r_customheight=720, r_customaspect=1280/720,
                    r_fullscreen=0, r_displayRefresh=0, r_swapInterval=0, r_ignorehwgamma=1,
                    com_maxfps=60, s_initsound=1, ui_TeamArenaFirstRun=1)
    (mod/'autoexec.cfg').write_text('\n'.join(f'set {k} "{v}"' for k, v in settings.items())+'\n')
    commands = ['wait 100', 'ui_test pad down']
    if args.players == 2: commands += ['ui_test pad right']
    commands += ['ui_test pad down', 'ui_test pad state', 'ui_test pad up', 'ui_test pad up',
                 'ui_test pad a', 'wait 300', 'devmap oasis', 'wait 300', 'local_join 1']
    if args.players == 2: commands += ['local_join 2']
    commands += ['ui_test script closeingame', 'map_restart 0 0', 'wait 300', 'set developer 1',
                 'wait 10', 'split_state', 'screenshot objectives-start']
    if args.players == 2:
        commands += ['gamepad_test 0 0 0 0 0 0 1', 'gamepad_test 1 0 0 0 0 0 1', 'wait 10',
                     'gamepad_test 1 32767 0 0 0 65536 1', 'wait 30',
                     'gamepad_test 1 0 0 0 0 0 1', 'wait 60', 'split_state', 'screenshot objectives-p2',
                     # Y's queue/owner is covered by the C input check: appended commands wait behind this entire cfg.
                     'split_weapnext', 'wait 120', 'split_state',
                     'split_weapnext', 'wait 120', 'split_state',
                     'split_weapnext', 'wait 120', 'split_state']
    commands += ['oasis_test state', 'oasis_test capture', 'wait 30', 'oasis_test wall', 'wait 60',
                 'oasis_test pump1', 'oasis_test pump2', 'wait 60', 'oasis_test alliedpost', 'oasis_test axispost',
                 'wait 60']
    if args.winner == 'allies':
        commands += ['oasis_test north', 'oasis_test south', 'wait 1200']
    else:
        commands += ['set timelimit 0.02', 'wait 1200']
    commands += ['oasis_test state']
    commands += ['cvarlist g_gametype', 'cvarlist timelimit',
                 'disconnect', 'wait 100', 'ui_test pad state', 'quit']
    (mod/'objective-check.cfg').write_text('\n'.join(commands)+'\n')
    result = subprocess.run([str(modules/'et.exe'), '+set', 'fs_basepath', str(ROOT/'runtime'),
                             '+set', 'fs_homepath', str(home), '+set', 'fs_game', 'et-rust',
                             '+set', 'com_recommendedSet', '1', '+set', 'com_introPlayed', '1',
                             '+exec', 'objective-check.cfg'], cwd=ROOT/'runtime', capture_output=True, timeout=160)
    text = (mod/'etconsole.log').read_text(encoding='latin1')
    assert result.returncode == 0 and '----- CL_Shutdown -----' in text, str(mod)
    assert not re.search(r'ERROR:|menu parse error|reliable command overflow', text.replace('ERROR: Disconnected from server', ''), re.I), str(mod)
    assert re.search(r'g_gametype[^\r\n]*"2"', text)
    assert 'timelimit=30' in text, 'Original map script did not set its timer.'
    assert 'OASIS human player=1 client=0 team=2 class=2 native_rules=1' in text
    assert Image.open(mod/'screenshots/objectives-start.tga').size == (1280, 720)
    if args.players == 2:
        assert 'OASIS human player=2 client=1 team=2 class=2 native_rules=1' in text
        assert 'SPLITSCREEN ready players=0,1 ai2=0' in text
        assert not re.search(r'Userinfo:.*?\\name\\Player 2.*?\\ip\\bot', text), 'Reserved human must retain localhost identity.'
        rows = re.findall(r'SPLITSTATE ids=0,1 ai2=0 .*?clip=(\d+),(\d+).*?pos1=([^ ]+) pos2=([^ ]+)', text)
        assert len(rows) == 5, rows
        assert rows[0][0] == rows[1][0] and int(rows[1][1]) < int(rows[0][1]), rows
        assert rows[0][2] == rows[1][2] and rows[0][3] != rows[1][3], rows
    else:
        assert 'SPLITSCREEN ready' not in text
    assert 'waves=30000,20000' in text
    weapons = re.findall(r'SPLITSTATE.*?weapon=(\d+),(\d+)', text)
    if args.players == 2:
        assert all(w[0] == weapons[0][0] for w in weapons) and weapons[1][1] != weapons[2][1], weapons
        assert {'21', '15'} <= {w[1] for w in weapons}, f'Player two must access pliers and dynamite: {weapons}'
    assert 'gamestate=0' in text and 'gamestate=3' in text, 'Original match must play and enter intermission.'
    actions = ('capture', 'wall', 'pump1', 'pump2', 'alliedpost', 'axispost')
    if args.winner == 'allies': actions += ('north', 'south')
    assert all(f'OASIS_TEST action={action}' in text for action in actions)
    assert f'OASIS_TEST winner={1 if args.winner == "allies" else 0}' in text
    states = re.findall(r'OASIS_TEST state.*?objectives=([^ ]+)', text)
    pairs = states[-1].strip('\\').split('\\')
    final = dict(zip(pairs[::2], pairs[1::2]))
    assert all(final[f'a{i}'] == '1' for i in range(3, 8)) and final['x8'] == '1', final
    if args.winner == 'allies':
        assert final['a1'] == final['a2'] == '1' and 'Allies have destroyed both Anti-Tank Guns' in text
    assert 'PAD_CHECK menu="main" item="START MATCH"' in text
    print(f'PASS original Oasis {args.players} player(s): native objective scripts, {args.winner} victory/intermission, reinforcements, controller isolation/tools and shutdown. {mod}')


if __name__ == '__main__':
    main()
