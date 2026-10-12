"""Real native browser + joypad keys against a private UDP master/info fixture.
Run with the game closed: python tests/server_browser.py. No public server is joined.
"""
import re
import select
import shutil
import socket
import struct
import subprocess
import threading
import time

from PIL import Image
from legacy_modes import console_command
from online_check import wait_log
from play_game import prepare_game
from smoke_server import ROOT


def main():
    modules = ROOT / 'build/slice/Release'
    home = modules / 'smoke/server-browser'
    shutil.rmtree(home, ignore_errors=True)  # Only this test's private profile.
    command = prepare_game(modules, home)
    mod = home / 'et-rust'
    master = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    master.bind(('127.0.0.1', 0))
    servers = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(20)]
    for server in servers:
        server.bind(('127.0.0.1', 0))
    protected = {5, 9, 13, 17}
    slots = {1: 32, 3: 31, 7: 29}  # Of 32: full, one free, three free.
    hostnames = [f'Browser server {i:02} - Long objectives' for i in range(20)]
    queries, joins = [], []
    stop = threading.Event()

    def respond():
        while not stop.is_set():
            ready, _, _ = select.select([master] + servers, [], [], .1)
            for server in ready:
                packet, sender = server.recvfrom(4096)
                if server is master:
                    assert packet.startswith(b'\xff' * 4 + b'getservers 84'), packet
                    queries.append(packet)
                    entries = b''.join(b'\\' + socket.inet_aton('127.0.0.1') +
                                       struct.pack('>H', s.getsockname()[1]) for s in servers)
                    master.sendto(b'\xff' * 4 + b'getserversResponse' + entries + b'\\EOT', sender)
                else:
                    index = servers.index(server)
                    if packet.startswith(b'\xff' * 4 + b'getinfo'):
                        # Include empty/full/nearly full servers and conflicting old advanced filters.
                        clients = slots.get(index, index)
                        info = dict(protocol=84, gamename='et', hostname=hostnames[index], mapname='oasis',
                                    clients=clients, sv_maxclients=32, gametype=2,
                                    needpass=(1 + index % 2) if index in protected else 0,
                                    friendlyFire=1, maxlives=0, punkbuster=0, g_antilag=1,
                                    weaprestrict=100, balancedteams=1, game='etmain')
                        text = ''.join(f'\\{k}\\{v}' for k, v in info.items())
                        server.sendto(b'\xff' * 4 + b'infoResponse\n' + text.encode() + b'\n', sender)
                    elif packet.startswith(b'\xff' * 4 + b'getchallenge'):
                        joins.append(index)  # Observe the actual target; never impersonate a game VM.

    thread = threading.Thread(target=respond, daemon=True)
    thread.start()
    settings = dict(r_mode=3, r_fullscreen=0, r_swapInterval=0, r_ignorehwgamma=1,
                    in_mouse=0, in_gamepad=0, in_gamepadIndex=3, cl_splitScreen=0, s_initsound=0, logfile=2, developer=0,
                    ui_rustMenuTest=1, com_maxfps=60, ui_netSource=1, ui_joinGameType=9,
                    ui_browserShowPasswordProtected=1, ui_browserShowEmptyOrFull=1,
                    ui_browserShowFriendlyFire=2, ui_browserShowMaxlives=1,
                    ui_browserShowPunkBuster=1, ui_browserShowAntilag=2,
                    ui_browserShowWeaponsRestricted=1, ui_browserShowTeamBalanced=2,
                    cl_master=f'127.0.0.1:{master.getsockname()[1]}')
    with (mod / 'autoexec.cfg').open('a') as config:
        config.write('\n'.join(f'set {k} "{v}"' for k, v in settings.items()) + '\n')
    (mod / 'browser-open.cfg').write_text('wait 60\nui_test pad state\nui_test pad down\nui_test pad state\nui_test pad up\nui_test pad a\nset BROWSER_OPEN 1\nBROWSER_OPEN\n')
    log = mod / 'etconsole.log'
    client = subprocess.Popen(command + ['+exec', 'browser-open.cfg'], cwd=home,
                              stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    serial = 0

    def phase(commands):
        nonlocal serial
        serial += 1
        marker = f'BROWSER_PHASE_{serial}_DONE'
        (mod / 'browser-phase.cfg').write_text('\n'.join(commands + [f'set {marker} 1', marker]) + '\n')
        console_command(client, 'exec browser-phase.cfg')
        wait_log(log, marker, client)
        return log.read_text(encoding='latin1').split(f'execing browser-phase.cfg')[-1]

    def capture(count):
        deadline = time.monotonic() + 20
        while True:
            text = phase(['ui_test browser', 'ui_test pad state'])
            match = re.search(r'BROWSER_CHECK source=(\d+) count=(\d+) current=(-?\d+) start=(-?\d+) sort=(\d+) direction=(\d+) refreshing=(\d+)', text)
            assert match, (text, log)
            state = tuple(map(int, match.groups()))
            if state[1] == count and not state[6]:
                rows = re.findall(r'BROWSER_SERVER index=(\d+) name="([^"]*)" password=(-?\d+)', text)
                assert len(rows) == count and all(p == '0' for _, _, p in rows), rows
                return state, rows
            assert time.monotonic() < deadline, (state, text, queries, thread.is_alive(), log)
            time.sleep(.5)

    def keys(*names):
        return [f'ui_test pad {name}' for name in names]

    try:
        wait_log(log, 'BROWSER_OPEN', client)
        state, rows = capture(15)
        assert state[0] == 1 and state[4:6] == (4, 0), state
        def names(hidden):
            return {hostnames[i] for i in range(20) if i not in protected | {1} | hidden}
        assert {name for _, name, _ in rows} == names(set())
        # Put the real UI pointer outside the browser: pad focus must still scroll.
        phase(['ui_test mouse -640 -480', 'wait 20', 'screenshot browser-public'] + keys('right', 'down', 'right') + ['ui_test pad state'])
        state, _ = capture(15)
        assert state[4:6] == (2, 1), state
        # Sort by name and return to the list through ordinary focus navigation.
        phase(keys('left', 'left', 'up', 'up') + ['ui_test pad state'])
        state, rows = capture(15)
        assert state[4:6] == (0, 0) and [n for _, n, _ in rows] == sorted(n for _, n, _ in rows), (state, rows)
        phase(keys(*(['down'] * 14)) + ['wait 20', 'screenshot browser-scrolled'])
        state, _ = capture(15)
        assert state[2] == 14 and state[3] > 0 and not joins, (state, joins)
        # Players is the shared split-screen count; servers need a free slot per local player.
        text = phase(keys('right', 'down', 'down', 'right') + ['ui_test pad state'])
        assert re.search(r'cvar="cl_splitScreen" value="1"', text, re.I), text
        state, rows = capture(14)
        assert {name for _, name, _ in rows} == names({3}), rows
        text = phase(keys('right') + ['ui_test pad state'])
        assert re.search(r'cvar="cl_splitScreen" value="3"', text, re.I), text
        state, rows = capture(14)
        assert {name for _, name, _ in rows} == names({3}), rows
        text = phase(keys('right') + ['ui_test pad state'])
        assert re.search(r'cvar="cl_splitScreen" value="4"', text, re.I), text
        state, rows = capture(13)
        assert {name for _, name, _ in rows} == names({3, 7}), rows
        text = phase(keys('left', 'left', 'left') + ['ui_test pad state'])
        assert re.search(r'cvar="cl_splitScreen" value="0"', text, re.I), text
        capture(15)
        phase(keys('up', 'up', 'right'))
        # Source now wraps only between Internet and LAN.

        state, _ = capture(0)
        assert state[0] == 0, state
        phase(['wait 20', 'screenshot browser-empty'] + keys('left'))
        capture(15)
        # An unreachable Internet master also exits refreshing without a keyboard.
        phase(['set cl_master "127.0.0.1:9"'] + keys('x'))
        capture(0)
        phase([f'set cl_master "127.0.0.1:{master.getsockname()[1]}"'] + keys('x'))
        capture(15)
        assert len(queries) >= 3 and not joins, (queries, joins)
        phase(keys('b') + ['ui_test pad state'])
        assert 'PAD_CHECK menu="main"' in log.read_text(encoding='latin1')
        phase(['ui_test script sliceBrowse'])
        capture(15)
        # One explicit A, not navigation/refresh, initiates a real connection.
        phase(keys('right', 'down', 'left', 'up', 'up', 'down', 'a'))
        deadline = time.monotonic() + 10
        while not joins:
            assert time.monotonic() < deadline, log
            time.sleep(.1)
        assert joins == [2], joins  # Name order, second row (full 01 hidden), no skipped/accidental join.
        console_command(client, 'quit')
        assert client.wait(timeout=15) == 0
        text = log.read_text(encoding='latin1')
        assert re.search(r'PAD_CHECK menu="main" item="PLAY ONLINE".*\n.*PADUI menu="main" item="START MATCH"', text), 'Main focus order must follow visual order'
        assert 'FAVORITE' not in (ROOT / 'native/ui/playonline.menu').read_text().upper()
        assert 'source=2' not in text, 'Favorites must not be reachable'
        assert not re.search(r'ERROR:|menu parse error|unknown menu keyword|unknown item keyword', text, re.I), log
        for name in ('browser-public', 'browser-scrolled', 'browser-empty'):
            image = Image.open(mod / f'screenshots/{name}.tga')
            assert image.size == (640, 480), image.size
            if name != 'browser-empty':
                # Selection must not paint over its own text or scroll offscreen.
                y = 110 if name == 'browser-public' else 352
                rgb = image.convert('RGB')
                assert rgb.getpixel((270, y))[1] > 45, (name, 'selected row not visible')
                pixels = rgb.crop((26, y, 260, y + 14)).getdata()
                assert sum(min(pixel) > 120 for pixel in pixels) > 30, (name, 'selected row text invisible')
                # The end of the cached 35-character hostname must fit before the map column.
                tail = rgb.crop((224, y - 6, 251, y + 14)).getdata()
                assert sum(min(pixel) > 120 for pixel in tail) > 10, (name, 'long hostname truncated')
                gap = rgb.crop((284, y - 6, 288, y + 14)).getdata()
                assert not any(min(pixel) > 120 for pixel in gap), (name, 'hostname overlaps map column')
        print(f'PASS simple browser: 15 public rows, password/free-slot filtering for 1/2/4 players, main-menu order, pad focus/scroll/sort/players/refresh/back/join, empty list, pointer outside list. {log}')
    finally:
        if client.poll() is None:
            client.terminate()
            client.wait(timeout=10)
        stop.set()
        thread.join(2)
        master.close()
        for server in servers:
            server.close()


if __name__ == '__main__':
    main()
