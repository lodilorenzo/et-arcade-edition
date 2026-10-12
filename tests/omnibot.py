"""Native balanced match population (humans included), actions and Tab rosters; game closed."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
from smoke_server import ROOT, MAPS


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--players', type=int, choices=(1, 2, 3, 4), default=2)
    parser.add_argument('--total', type=int, choices=(4, 8, 12, 16, 24, 32, 40, 48), default=24)
    parser.add_argument('--seconds', type=int, default=120)
    parser.add_argument('--scoreboard', action='store_true', help='Check Tab rosters in every local view.')
    parser.add_argument('--rebalance', action='store_true', help='Switch player one to Axis, Allies, spectator and back through the stock limbo panel.')
    parser.add_argument('--observe', action='store_true', help='Spectator humans; bots fill the active match.')
    parser.add_argument('--objectives', action='store_true', help='Oasis only: require an autonomous Allied victory (use --observe --seconds 1200).')
    parser.add_argument('--map', choices=MAPS, default='oasis')
    args = parser.parse_args()
    assert 120 <= args.seconds <= 1200
    assert not args.objectives or args.map == 'oasis'
    assert not (args.observe and args.rebalance)
    requested = args.total
    if args.players == 4: args.total = max(8, args.total)
    bots = args.total - (0 if args.observe else args.players)
    modules = ROOT/'build/slice/Release'
    home = modules/f'smoke/omnibot-{args.players}-total{args.total}{"-observe" if args.observe else ""}{"-rebalance" if args.rebalance else ""}-{args.map}'
    shutil.rmtree(home, ignore_errors=True)
    mod = home/'et-rust'; mod.mkdir(parents=True)
    for name in ('qagame', 'cgame', 'cgame2', 'cgame3', 'cgame4', 'ui'):
        shutil.copy2(modules/f'{name}_mp_x86.dll', ROOT/'runtime/et-rust')
    shutil.copytree(ROOT/'native/ui', ROOT/'runtime/et-rust/ui', dirs_exist_ok=True)
    settings = dict(ui_rustSlice=1, ui_rustMenuTest=1, ui_matchPlayers=requested, ui_oasisMap=args.map,
                    ui_oasisTeam1=3 if args.observe else 2, ui_oasisTeam2=3 if args.observe else 2,
                    ui_oasisClass1=2, ui_oasisClass2=2,
                    in_mouse=0, in_joystick=0, in_gamepadIndex=3, in_gamepadIndex2=2,
                    cl_splitScreen=args.players if args.players >= 3 else int(args.players == 2), developer=1, logfile=2, com_hunkMegs=256,
                    g_log='omnibot-actions.log', g_logSync=1,
                    sv_pure=0, net_ip='127.0.0.1', net_port=27976, bot_enable=0,
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='',
                    cl_autoupdate=0, cl_punkbuster=0, sv_punkbuster=0,
                    r_mode=3, r_fullscreen=0, r_swapInterval=0, r_ignorehwgamma=1,
                    com_maxfps=60, s_initsound=0, ui_TeamArenaFirstRun=1)
    for player in range(3, 5):
        settings[f'ui_oasisTeam{player}'] = 3 if args.observe else 2
        settings[f'ui_oasisClass{player}'] = 2
    (mod/'autoexec.cfg').write_text('\n'.join(f'set {k} "{v}"' for k, v in settings.items())+'\n')
    commands = ['wait 100', 'ui_test script sliceStart', 'wait 1200', f'devmap {args.map}',
                'wait 300', 'local_join 1']
    commands += [f'local_join {player}' for player in range(2, args.players + 1)]
    commands += ['ui_test script closeingame', 'map_restart 0 0',
                 f'wait {3600 if args.total > 12 else 1200}', 'set developer 1', 'com_maxfps 60',
                 'wait 10', 'omnibot_test', 'oasis_test state']
    if args.scoreboard:
        commands += ['+scores', 'wait 60', 'scoreboard_test']
        for player in range(2, args.players + 1):
            commands += [f'split_cgame {player} +scores', 'wait 60', f'split_cgame {player} scoreboard_test']
        commands += ['screenshot scoreboard', '-scores']
        commands += [f'split_cgame {player} -scores' for player in range(2, args.players + 1)]
    if args.rebalance:
        # Initial focus is Class. Team choices are Allies/Axis/Spectator.
        for directions in (['right'], ['left'], ['right', 'right'], ['right']):
            commands += ['local_class 1', 'wait 10', 'loadout_test key up']
            commands += [f'loadout_test key {d}' for d in directions]
            commands += ['loadout_test key a', 'loadout_test key down', 'loadout_test key down',
                         'loadout_test key down', 'loadout_test key a', 'wait 600',
                         'oasis_test player 0', 'omnibot_test', 'oasis_test state']
    for _ in range(args.seconds // 10):
        commands += ['omnibot_test', 'oasis_test state', 'split_state', 'wait 600']
    commands += ['omnibot_test', 'oasis_test state', 'screenshot omnibot', 'disconnect', 'wait 100', 'quit']
    (mod/'omnibot-check.cfg').write_text('\n'.join(commands)+'\n')
    result = subprocess.run([str(modules/'et.exe'), '+set', 'fs_basepath', str(ROOT/'runtime'),
                             '+set', 'fs_homepath', str(home), '+set', 'fs_game', 'et-rust',
                             '+set', 'com_recommendedSet', '1', '+set', 'com_introPlayed', '1',
                             '+exec', 'omnibot-check.cfg'], cwd=ROOT/'runtime', capture_output=True,
                            timeout=(args.seconds + (210 if args.rebalance else 150)) * (2 if args.players == 4 else 1))
    text = (mod/'etconsole.log').read_text(encoding='latin1')
    assert result.returncode == 0 and '----- CL_Shutdown -----' in text, str(mod)
    assert 'Omni-bot Loaded Successfully' in text, str(mod)
    assert not re.search(r'ERROR:|menu parse error|reliable command overflow', text.replace('ERROR: Disconnected from server', ''), re.I), str(mod)
    counts = re.findall(r'OMNIBOT_TEST count=(\d+) requested=(\d+) axis=(\d+) allies=(\d+) humans=(\d+)\nOASIS_TEST state [^\n]*gamestate=(\d+)', text)
    assert counts, 'Missing population checkpoints'
    for c, requested, axis, allies, humans, state in counts:
        assert int(requested) == args.total, requested
        if state == '3' and c == '0': continue  # Upstream removes bots at game end.
        assert int(axis) == int(allies) == args.total // 2, ('Unequal team population', axis, allies)
        assert int(c) == args.total - int(humans), ('Humans must replace bots', c, humans)
        assert int(humans) == (0 if args.observe else args.players) or (args.rebalance and int(humans) == args.players - 1), counts
    if args.rebalance:
        teams = re.findall(r'OASIS_TEST player client=0 team=(\d+)', text)
        assert teams == ['1', '2', '3', '2'], ('Original limbo team changes failed', teams)
    rows = re.findall(r'OMNIBOT_TEST client=(\d+) team=(\d+) class=(\d+) health=(-?\d+) weapon=(\d+) time=(\d+) pos=([^ ]+) move=([^ ]+) buttons=(\d+)', text)
    if args.total // 2 > args.players or args.observe:
        assert {r[1] for r in rows} >= {'1', '2'}, rows
    else:
        assert '1' in {r[1] for r in rows}, rows  # Both humans can fill the small Allied team.
    populations = re.findall(r'((?:OMNIBOT_TEST client=[^\n]+\n)+)OMNIBOT_TEST count=(\d+)', text)
    assert populations, 'Missing bot population checkpoints'
    for population, count in populations:
        teams = re.findall(r'team=(\d+)', population)
        assert len(teams) == int(count), (count, teams)
        assert teams.count('1') <= args.total // 2 and teams.count('2') <= args.total // 2
    if bots >= 4:
        assert {'1', '2'} <= {r[2] for r in rows}, 'Medics and engineers required.'
        assert re.search(r'OMNIBOT_WEAPON client=\d+ native=21 bot=37', text), 'Native pliers must translate to Omni-bot IDs.'
    if bots >= 6: assert '3' in {r[2] for r in rows}, 'Field ops required.'
    if bots >= 8: assert '0' in {r[2] for r in rows}, 'Soldiers required.'
    assert any(r[7] != '0,0,0' for r in rows), 'No bot movement commands.'
    assert any(int(r[8]) & 1 for r in rows), 'No bot fire/tool commands.'
    positions = {}
    for r in rows: positions.setdefault(r[0], set()).add(r[6])
    # At the four-human minimum, two Axis engineers can legitimately stay at mine goals.
    moving = 2 if args.players == 4 and bots == 4 else min(4, bots)
    assert sum(len(p) > 1 for p in positions.values()) >= moving, positions
    if args.players > 1:
        assert 'SPLITSCREEN ready players=' + ','.join(map(str, range(args.players))) + ' ai2=0' in text
        for player in range(1, args.players):
            assert str(player) not in positions, 'Local humans must not be AI.'
    assert f'Server: {args.map}' in text and 'Loaded Waypoints.' in text
    if args.scoreboard:
        boards = re.findall(r'SCOREBOARD_TEST begin player=(\d+) scores=(\d+)\n(.*?)SCOREBOARD_TEST end axis=(\d+) allies=(\d+)', text, re.S)
        assert len(boards) == args.players, ('Missing scoreboard checks', boards)
        assert {int(board[0]) for board in boards} == set(range(args.players))
        expected_ids = {int(i) for i in re.findall(r'client=(\d+)', populations[0][0])}
        if not args.observe: expected_ids.update(range(args.players))
        for player, count, rendered, axis, allies in boards:
            entries = re.findall(r'SCOREBOARD_ROW client=(\d+) team=(\d+) bottom=(\d+)', rendered)
            ids = [int(row[0]) for row in entries]
            assert int(count) == args.total + (args.players if args.observe else 0), ('Incomplete scores', player, count)
            assert len(ids) == args.total and len(set(ids)) == len(ids), ('Incomplete/duplicate roster', player, entries)
            assert set(ids) == expected_ids, ('Missing client IDs', player, ids, expected_ids)
            assert all(int(row[2]) <= 480 for row in entries), ('Rows outside screen', player, entries)
            assert int(axis) == int(allies) == args.total // 2
            assert sum(row[1] == '1' for row in entries) == sum(row[1] == '2' for row in entries) == args.total // 2
        print(f'PASS Tab scoreboard: {args.total} unique active players, equally populated visible rosters in every local view.')
    print(f'PASS Omni-bot on {args.map}: {args.total} total = {args.players if not args.observe else 0} humans + {bots} bots; {args.total // 2} per team, native actions and clean shutdown. {mod}')
    actions = (mod/'omnibot-actions.log').read_text(encoding='latin1')
    print('Native action evidence: ' + ', '.join(f'{event}={actions.count(event)}' for event in ('Dynamite_Plant:', 'Dynamite_Diffuse:', 'Medic_Revive:', 'Health_Pack:', 'Ammo_Pack:', 'Kill:')))
    if args.objectives:
        assert args.observe
        assert 'OASIS_TEST winner=1' in text and any(s == '3' for *_, s in counts), 'No Allied victory/intermission.'
        for event in ('Allies capture the Old City!', 'Allies have built the Oasis Water Pump!',
                      'Allies have built the Old City Water Pump!', 'Allies have breached the Old City wall',
                      'Allied Command Post constructed.', 'Allies have destroyed both Anti-Tank Guns!'):
            assert event in actions, f'Missing native action: {event}'
        assert 'Dynamite_Plant:' in actions
        print('PASS autonomous original Oasis objectives and native Allied victory/intermission.')


if __name__ == '__main__':
    main()
