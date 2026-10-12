"""Local map list: downloaded maps offline with/without bots, mod maps excluded.
python tests/map_list.py. Opens a game window; fixtures are renamed copies of your original stock maps.
"""
import re
import shutil
import subprocess
import time
import zipfile

from PIL import Image
from legacy_modes import console_command
from online_check import wait_log
from play_game import prepare_game
from smoke_server import ROOT

STOCK = ['battery', 'fueldump', 'goldrush', 'oasis', 'radar', 'railgun']


def fixture(path, name, source, script=None, arena='wolfmp wolfsw', extra=None, bsp=True):
    """A map pack built from a stock map: maps/<name>.*, its lightmap folder and level shot."""
    stock = zipfile.ZipFile(ROOT / 'runtime/etmain/pak0.pk3')
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_STORED) as out:
        for entry in stock.namelist():
            low = entry.lower()
            if low.startswith(f'maps/{source}') and '_lms' not in low and not low.endswith('/'):
                rest = entry[len(f'maps/{source}'):]
                if rest == '.script' or (rest == '.bsp' and not bsp):
                    continue
                out.writestr(f'maps/{name}{rest}', stock.read(entry))
            elif low == f'levelshots/{source}.tga':
                out.writestr(f'levelshots/{name}.tga', stock.read(entry))
        text = stock.read(f'maps/{source}.script').decode('latin1')
        if script:
            text = script(text)
        if script is not False:
            out.writestr(f'maps/{name}.script', text)
        if arena:
            out.writestr(f'scripts/{name}.arena', f'{{\n map "{name}"\n longname "Fixture {name}"\n type "{arena}"\n}}\n')
        for entry, data in (extra or {}).items():
            out.writestr(entry, data)


def main():
    modules = ROOT / 'build/slice/Release'
    home = modules / 'smoke/map-list'
    shutil.rmtree(home, ignore_errors=True)  # Only this check's private profile.
    command = prepare_game(modules, home)
    etmain, mod = home / 'etmain', home / 'et-rust'
    # Listed: bot navigation / no navigation / downloaded by split-screen player 2 (copied in).
    fixture(etmain / 'zz_fixture_bots.pk3', 'fixture_bots', 'oasis')
    fixture(etmain / 'zz_fixture_nobots.pk3', 'fixture_nobots', 'radar')
    fixture(home / 'players/2/etmain/zz_fixture_split.pk3', 'fixture_split', 'fueldump')
    # Excluded: mod-only script action, LMS-only arena, no objective script, native code, mod folder.
    fixture(etmain / 'zz_fixture_mod.pk3', 'fixture_mod', 'battery',
            script=lambda t: t.replace('game_manager\r\n{\r\n\tspawn\r\n\t{', 'game_manager\r\n{\r\n\tspawn\r\n\t{\r\n\t\tcreate\r\n\t\t{\r\n\t\t}', 1))
    fixture(etmain / 'zz_fixture_lms.pk3', 'fixture_lms', 'battery', arena='wolflms')
    fixture(etmain / 'zz_fixture_noscript.pk3', 'fixture_noscript', 'battery', script=False)
    fixture(home / 'players/2/etmain/zz_fixture_code.pk3', 'fixture_code', 'battery', extra={'cgame_mp_x86.dll': b'MZ'})
    fixture(home / 'fixturemod/zz_fixture_moddir.pk3', 'fixture_moddir', 'battery')
    # A private Omni-bot folder: navigation for the stock maps and fixture_bots only.
    bots = home / 'omnibot'
    shutil.copytree(ROOT / 'runtime/omnibot', bots, ignore=shutil.ignore_patterns('nav'))
    (bots / 'et/nav').mkdir(parents=True)
    for map_name in STOCK:
        for suffix in ('.way', '.gm', '_goals.gm'):
            shutil.copy2(ROOT / f'runtime/omnibot/et/nav/{map_name}{suffix}', bots / f'et/nav/{map_name}{suffix}')
    for suffix in ('.way', '.gm', '_goals.gm'):
        shutil.copy2(ROOT / f'runtime/omnibot/et/nav/oasis{suffix}', bots / f'et/nav/fixture_bots{suffix}')
    settings = dict(r_mode=-1, r_customwidth=1280, r_customheight=720, r_customaspect=1280/720, r_fullscreen=0,
                    r_swapInterval=0, r_ignorehwgamma=1, in_mouse=0, in_gamepad=0, s_initsound=0, logfile=2, developer=1,
                    ui_rustMenuTest=1, com_maxfps=60, cl_master='127.0.0.1:9', cl_splitScreen=0, ui_oasisMap='oasis',
                    ui_matchPlayers=8, omnibot_path=str(bots))
    with (mod / 'autoexec.cfg').open('a') as config:
        config.write('\n'.join(f'set {k} "{v}"' for k, v in settings.items()) + '\n')
    log = mod / 'etconsole.log'
    client = subprocess.Popen(command, cwd=home, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    serial = 0

    def phase(commands, marker_wait=60):
        nonlocal serial
        serial += 1
        marker = f'MAPLIST_PHASE_{serial}_DONE'
        (mod / 'map-phase.cfg').write_text('\n'.join(commands + [f'set {marker} 1', marker]) + '\n')
        console_command(client, 'exec map-phase.cfg')
        deadline = time.monotonic() + marker_wait
        while marker not in log.read_text(encoding='latin-1'):
            assert time.monotonic() < deadline and client.poll() is None, (marker, log)
            time.sleep(.2)
        return log.read_text(encoding='latin-1').split('execing map-phase.cfg')[-1]

    try:
        wait_log(log, 'MAPLIST maps=', client)
        time.sleep(3)
        text = log.read_text(encoding='latin-1')
        assert 'MAPLIST copied zz_fixture_split.pk3 from player 2' in text, log
        assert 'zz_fixture_code.pk3' not in text and not (etmain / 'zz_fixture_code.pk3').exists(), 'Pack with native code copied'
        listed = [line.split('\t') for line in (mod / 'maplist.txt').read_text(encoding='latin-1').splitlines()]
        names = [row[0].lower() for row in listed]
        assert sorted(names) == sorted(STOCK + ['fixture_bots', 'fixture_nobots', 'fixture_split']), names
        with_bots = [row[0].lower() for row in listed if row[1] == '1']
        assert sorted(with_bots) == sorted(STOCK + ['fixture_bots']), listed
        assert names[:len(with_bots)] == with_bots, 'Bot maps must be listed first'
        excluded = dict(re.findall(r'MAPLIST excluded map=(\S+) reason=([^\n]*)', text))
        assert excluded.get('fixture_mod') == 'unknown action create', excluded
        assert excluded.get('fixture_lms') == 'no objective game type', excluded
        assert excluded.get('fixture_noscript') == 'no objective script', excluded
        assert 'fixture_moddir' not in text, 'Mod-folder map scanned'
        # Controller: main menu Map -> chooser on the current map -> down to the next one -> A picks it.
        order = ['PLAY ONLINE', 'START MATCH', 'Players:', 'Map:']
        first = re.findall(r'PAD_CHECK menu="main" item="([^"]*)"', phase(['wait 30', 'ui_test pad state']))[-1]
        downs = ['ui_test pad down'] * (order.index('Map:') - order.index(first))
        state = phase(downs + ['ui_test pad state', 'ui_test pad a', 'wait 20', 'ui_test pad state', 'wait 10', 'screenshot map-chooser'])
        pads = re.findall(r'PAD_CHECK menu="([^"]*)" item="([^"]*)" cvar="([^"]*)" value="([^"]*)"', state)
        assert pads[0][:2] == ('main', 'Map:'), pads
        assert pads[1][0] == 'slice_maps' and pads[1][2].lower() == 'ui_rustmappick' and pads[1][3] == 'oasis', pads
        following = names[names.index('oasis') + 1]
        state = phase(['ui_test pad down', 'ui_test pad state', 'ui_test pad a', 'wait 10', 'ui_test pad state', 'ui_oasisMap', 'ui_rustMapLabel'])
        pads = re.findall(r'PAD_CHECK menu="([^"]*)" item="([^"]*)" cvar="([^"]*)" value="([^"]*)"', state)
        assert pads[0][3] == following and pads[1][0] == 'main', (pads, following)
        assert re.search(rf'"ui_oasisMap" is:"{following}', state, re.I), state
        Image.open(mod / 'screenshots/map-chooser.tga').save(ROOT / 'build/map-chooser.png')
        # Bot map: Omni-bot loads its navigation and fills the teams.
        state = phase(['set ui_oasisMap fixture_bots', 'ui_test script sliceStart', 'wait 600', 'mapname'], 120)
        assert 'SLICESTART map=fixture_bots bots=1 total=8' in state and '"mapname" is:"fixture_bots' in state, state[-3000:]
        assert 'Omni-bot Loaded Successfully' in state, state[-3000:]
        deadline = time.monotonic() + 60
        while log.read_text(encoding='latin-1').split('SLICESTART map=fixture_bots')[-1].count('entered the game') < 4:
            assert time.monotonic() < deadline and client.poll() is None, 'Bots did not join fixture_bots'
            time.sleep(.5)
        # No-bot map: humans only, Omni-bot not started; the copied split-screen download plays too.
        for map_name in ('fixture_nobots', 'fixture_split'):
            state = phase(['disconnect', 'wait 120', f'set ui_oasisMap {map_name}', 'ui_test script sliceStart', 'wait 600', 'mapname',
                           'omnibot_enable', f'screenshot map-{map_name}'], 120)
            assert f'SLICESTART map={map_name} bots=0 total=0' in state and f'"mapname" is:"{map_name}' in state, state[-3000:]
            assert 'Omni-bot Loaded Successfully' not in state and re.search(r'"omnibot_enable" is:"0', state), state[-3000:]
        # An unlisted map name falls back to Oasis rather than loading it.
        state = phase(['disconnect', 'wait 120', 'set ui_oasisMap fixture_mod', 'ui_test script sliceStart', 'wait 300', 'mapname'], 120)
        assert 'SLICESTART map=oasis bots=1' in state and '"mapname" is:"oasis' in state, state[-3000:]
        console_command(client, 'quit')
        assert client.wait(timeout=30) == 0
        text = log.read_text(encoding='latin-1')
        assert not re.search(r'menu parse error|unknown (?:menu|item) keyword|G_Script_ScriptParse', text, re.I), log
        print(f'PASS map list: {len(names)} maps ({len(with_bots)} with bots, listed first), mod/LMS/no-script maps excluded, '
              f'split-screen download copied, native-code pack refused, chooser by controller, bot and human-only matches. {log}')
    finally:
        if client.poll() is None:
            client.terminate()
            client.wait(timeout=10)


if __name__ == '__main__':
    main()
