"""Two (side-by-side), three (2x2 with live map) or four real pure-server clients embedded in one native window.
python tests/online_split.py [--legacy] [--players 2|3|4]. Only a trusted localhost server is joined.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import time

from PIL import Image
from legacy_modes import console_command, KEY, GUID
from online_check import wait_log
from play_game import prepare_game
from play_online import prepare_online
from smoke_server import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy', action='store_true')
    parser.add_argument('--players', type=int, choices=(2, 3, 4), default=4, help='Local split-screen players joining online')
    parser.add_argument('--benchmark', action='store_true', help='Compare direct/async readback in the same four-client session')
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    parser.add_argument('--no-desktop-input', action='store_true', help='Skip checks that move the real cursor or press real keys')
    args = parser.parse_args()
    width, height = args.width, args.height
    assert 640 <= width <= 16384 and 480 <= height <= 16384 and not (width % 2 or height % 2)
    n = args.players
    tag = {2: 'two', 3: 'three', 4: 'four'}[n]
    assert n >= 3 or height <= 8192
    pw, ph = width // 2, height // 2 if n >= 3 else height
    # Player setup: controllers in reverse (view 1 = last controller); keyboard & mouse to the last view.
    pads = list(range(n))[::-1] + [-1] * (4 - n)
    pads[n - 1] = -2  # Keyboard/mouse is a separate device, not an extra controller player.
    controlled = n - 2
    modules = ROOT / 'build/slice/Release'
    test = modules / (('smoke/online-split-legacy' if args.legacy else 'smoke/online-split-stock') + ('' if n == 4 else f'-{n}'))
    home = test / 'client'
    command = prepare_game(modules, home)
    (home / 'etmain/etkey').write_bytes(KEY)  # Only this fixture's identity.
    server_home = test / 'server'
    game = 'legacy' if args.legacy else 'etmain'
    reference = ROOT / 'reference/etlegacy-runtime'
    if args.legacy:
        server_engine = reference / 'etlded.exe'
        for base in (server_home, home):
            (base / 'legacy').mkdir(parents=True, exist_ok=True)
            shutil.copy2(reference / 'legacy/legacy_v2.86.0.pk3', base / 'legacy')
        shutil.copy2(reference / 'legacy/qagame_mp_x86.dll', server_home / 'legacy')
    else:
        server_engine = ROOT / 'runtime/etded.exe'
        prepare_online(server_engine, server_home)
        shutil.copy2(modules / 'qagame_mp_x86.dll', server_home / 'etmain')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as port:
        port.bind(('127.0.0.1', 0))
        server_port = port.getsockname()[1]
    settings = dict(net_ip='127.0.0.1', net_port=server_port, dedicated=1, sv_pure=1, sv_maxclients=8,
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='', sv_master5='',
                    sv_punkbuster=0, cl_punkbuster=0, omnibot_enable=0, bot_enable=0,
                    g_doWarmup=0, g_gametype=2, g_teamForceBalance=0, g_maxGameClients=0, team_maxplayers=0, g_inactivity=0,
                    g_spectatorInactivity=0, logfile=2, com_hunkMegs=256, rconPassword='four-fixture',
                    com_recommendedSet=1, developer=1)
    server_mod = server_home / game
    (server_mod / 'autoexec.cfg').write_text('\n'.join(f'set {k} "{v}"' for k, v in settings.items()) + '\n')
    if args.legacy:
        settings['lua_modules'] = 'four-fixture.lua'
        (server_mod / 'autoexec.cfg').write_text('\n'.join(f'set {k} "{v}"' for k, v in settings.items()) + '\n')
        (server_mod / 'four-fixture.lua').write_text('''function et_RunFrame(t)
 et.trap_Cvar_Set("g_bluelimbotime", "1000")
 et.trap_Cvar_Set("g_redlimbotime", "1000")
end
function et_ConsoleCommand(command)
 if command ~= "fixture_notice" then return 0 end
 et.trap_SendServerCommand(-1, 'cp "Your client is outdated. QUIET_CENTER_ADVISORY"')
 et.trap_SendServerCommand(-1, 'cpm "You are running old software. QUIET_POPUP_ADVISORY"')
 et.trap_SendServerCommand(-1, 'print "Please update your client. QUIET_PRINT_ADVISORY\\\\n"')
 et.trap_SendServerCommand(-1, 'chat "Your client is outdated. QUIET_CHAT_ADVISORY" -1')
 et.trap_SendServerCommand(-1, 'print "Your client is outdated: connection refused. VISIBLE_REJECTION\\\\n"')
 et.trap_SendServerCommand(-1, 'print "Bad GUID: Invalid etkey. VISIBLE_IDENTITY_ERROR\\\\n"')
 et.trap_SendServerCommand(-1, 'chat "Ordinary player chat. VISIBLE_PLAYER_CHAT" 0')
 return 1
end
''')
    server_log = server_mod / 'etconsole.log'
    server_log.unlink(missing_ok=True)
    server = subprocess.Popen([str(server_engine), '+set', 'fs_basepath', str(ROOT / 'runtime' if args.legacy else server_home),
                               '+set', 'fs_homepath', str(server_home), '+set', 'fs_game', game,
                               '+set', 'dedicated', '1', '+set', 'net_enabled', '1', '+set', 'db_mode', '0', '+map', 'oasis'],
                              cwd=server_home, creationflags=subprocess.CREATE_NEW_CONSOLE)
    client = None
    handles = []
    user = c.WinDLL('user32', use_last_error=True)
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetParent.argtypes = [w.HWND]; user.GetParent.restype = w.HWND
    user.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

    class Child:
        def __init__(self, pid):
            self.pid = pid
            self.handle = kernel.OpenProcess(0x1000 | 0x100000, False, pid)
            assert self.handle, pid
            handles.append(self.handle)
        def poll(self):
            code = w.DWORD()
            assert kernel.GetExitCodeProcess(self.handle, c.byref(code))
            return None if code.value == 259 else code.value

    def logs(slot):
        return '\n'.join(p.read_text(encoding='latin1') for p in (home / f'players/{slot}').rglob('etconsole.log'))

    def wait(predicate, message, process, seconds=90):
        deadline = time.monotonic() + seconds
        while not predicate():
            assert process.poll() is None, (message, 'process exited', test)
            assert time.monotonic() < deadline, (message, test)
            time.sleep(.1)

    def windows(pid):
        found = []
        @callback
        def visit(hwnd, unused):
            owner = w.DWORD(); name = c.create_unicode_buffer(80)
            user.GetWindowThreadProcessId(hwnd, c.byref(owner))
            user.GetClassNameW(hwnd, name, len(name))
            if owner.value == pid and name.value == 'ET Online Split': found.append(hwnd)
            return True
        user.EnumWindows(visit, 0)
        return found

    try:
        wait_log(server_log, '------- Game Initialization -------', server)
        extra = dict(r_mode=-1, r_customwidth=width, r_customheight=height, r_customaspect=width/height, r_fullscreen=0,
                     r_displayRefresh=0, r_swapInterval=0, r_ignorehwgamma=1, in_mouse=1, in_gamepad=0,
                     s_initsound=0, developer=0, ui_rustMenuTest=1,
                     logfile=2, com_maxfps=60, cl_splitScreen=n if n >= 3 else 1, name='OnlineFour', cl_master='127.0.0.1:9',
                     cl_gamepadRadialHelp=0, cl_gamepadAssistStrength=1.6, cl_keyboardPlayer=n,
                     **{('in_gamepadIndex' + (str(i + 1) if i else '')): pads[i] for i in range(4)})
        with (home / 'et-rust/autoexec.cfg').open('a') as config:
            config.write('\n'.join(f'set {k} "{v}"' for k, v in extra.items()) + '\n')
        # Clear only fixture logs from a preceding run, not its persistent keys/profiles.
        for log in home.rglob('etconsole.log'): log.unlink()
        (home / 'et-rust/four-online.cfg').write_text(f'wait 60\nui_test script closeingame\nconnect 127.0.0.1:{server_port}\n')
        parent_log = home / 'et-rust/etconsole.log'
        primary_config = sorted(home.rglob('etconfig.cfg'))
        before = {p: hashlib.sha256(p.read_bytes()).digest() for p in primary_config if 'players' not in p.parts}
        client = subprocess.Popen(command + ['+exec', 'four-online.cfg'], cwd=home,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        wait_log(parent_log, f'ONLINESPLIT ready clients={n} layout={"2x2" if n >= 3 else "2x1"}', client)
        before = {p: hashlib.sha256(p.read_bytes()).digest() for p in primary_config if 'players' not in p.parts}
        spawned = re.findall(r'ONLINESPLIT spawned player=(\d) pid=(\d+)', parent_log.read_text(encoding='latin1'))
        assert len(spawned) == n, spawned
        for slot in range(1, n + 1):  # Launcher menu choices reach every online view.
            pane_cfg = (home / f'players/{slot}/etmain/online-pane.cfg').read_text()
            assert 'set cl_gamepadRadialHelp 0' in pane_cfg and 'set cl_gamepadAssistStrength 1.6' in pane_cfg, pane_cfg
        children = [Child(int(pid)) for slot, pid in spawned]
        host = windows(client.pid)
        assert len(host) == 1, host
        host = host[0]
        actual_panes = []
        @callback
        def pane(hwnd, unused):
            owner = w.DWORD(); name = c.create_unicode_buffer(80)
            user.GetWindowThreadProcessId(hwnd, c.byref(owner)); user.GetClassNameW(hwnd, name, len(name))
            if name.value == 'Enemy Territory': actual_panes.append((owner.value, hwnd))
            return True
        user.EnumChildWindows.argtypes = [w.HWND, c.c_void_p, w.LPARAM]
        user.EnumWindows(pane, 0)
        actual_panes = [(pid, hwnd) for pid, hwnd in actual_panes if pid in {p.pid for p in children}]
        views = re.findall(r'ONLINESPLIT view player=(\d) source=(\d+) rect=(\d+),(\d+),(\d+),(\d+)', parent_log.read_text(encoding='latin1'))
        assert {tuple(map(int, view[2:])) for view in views} == {((i % 2) * pw, (i // 2) * ph, (i % 2 + 1) * pw, (i // 2 + 1) * ph) for i in range(n)}, views
        assert {pid for pid, hwnd in actual_panes} == {p.pid for p in children}, actual_panes
        host_rect = w.RECT(); user.GetClientRect(host, c.byref(host_rect))
        assert (host_rect.right, host_rect.bottom) == (width, height), (host_rect.right, host_rect.bottom)
        for pid, hwnd in actual_panes:
            rect = w.RECT(); user.GetClientRect(hwnd, c.byref(rect))
            assert not user.GetParent(hwnd) and (rect.right, rect.bottom) == (pw, ph), (pid, rect.right, rect.bottom)
        guids, ids = [], []
        for slot, child in enumerate(children, 1):
            text = logs(slot)
            match = re.search(r'ONLINEPANE ready player=(\d) client=(\d+) guid=([A-F0-9]{32})', text)
            assert match and int(match[1]) == slot, (slot, text[-2000:])
            ids.append(int(match[2])); guids.append(match[3])
            mod = home / f'players/{slot}' / game
            mod.mkdir(exist_ok=True)
            team = 'b 2 8 7' if slot % 2 else 'r 2 3 2'
            (mod / 'four-check.cfg').write_text(f'cmd team {team}\nwait 120\nonlinepane_test\n')
            console_command(child, f'gamepad_test {pads[slot - 1]} 0 0 0 0 0 1; exec four-check.cfg')
        assert len(set(ids)) == len(set(guids)) == n and guids[0] == GUID, (ids, guids)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as rcon:
            for name in ('g_bluelimbotime', 'g_redlimbotime'):
                rcon.sendto(b'\xff' * 4 + f'rcon four-fixture {name} 1000'.encode(), ('127.0.0.1', server_port))
        for slot, child in enumerate(children, 1):
            def alive():
                if args.legacy:
                    console_command(child, 'cmd team ' + ('b 2 8 7' if slot % 2 else 'r 2 3 2'))
                    time.sleep(1.2)  # Respect Legacy's IP/userinfo flood interval.
                console_command(child, 'onlinepane_test')
                time.sleep(.2)
                matches = re.findall(r'ONLINEPANE_TEST .* health=(\d+) pm=(\d+)', logs(slot))
                return bool(matches and matches[-1] == ('100', '0'))
            wait(alive, f'player {slot} spawn', child, 50)
        def state(slot):
            text = logs(slot)
            matches = re.findall(r'ONLINEPANE_TEST .* controller=(-?\d+) .* pos=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+) yaw=(-?[\d.]+)', text)
            assert matches, text[-2000:]
            return tuple(map(float, matches[-1]))
        initial = [state(slot) for slot in range(1, n + 1)]
        assert [int(v[0]) for v in initial] == pads[:n], initial
        # The last controller player moves; the final player uses keyboard/mouse only.
        console_command(children[controlled], f'gamepad_test {pads[controlled]} 0 0 0 0 0 1')
        time.sleep(.3)
        console_command(children[controlled], f'gamepad_test {pads[controlled]} 0 26000 26000 0 0 1')
        time.sleep(1.2)
        console_command(children[controlled], f'gamepad_test {pads[controlled]} 0 0 0 0 0 1')
        time.sleep(.4)
        for slot, child in enumerate(children, 1):
            (home / f'players/{slot}/{game}/screenshots/four-online.tga').unlink(missing_ok=True)
            console_command(child, 'onlinepane_test; screenshot four-online')
        time.sleep(1)
        after = [state(slot) for slot in range(1, n + 1)]
        for slot in range(n):
            if slot == controlled: continue
            assert sum((a - b) ** 2 for a, b in zip(initial[slot][1:4], after[slot][1:4])) < 4, (slot, initial, after)
        assert abs(after[controlled][4] - initial[controlled][4]) > 10, (initial, after)
        assert sum((a - b) ** 2 for a, b in zip(initial[controlled][1:4], after[controlled][1:4])) > 100, (initial, after)
        # Keyboard routing without the real desktop: key messages posted to the split window reach only the
        # keyboard view. S walks backwards (stock binding): the controller check above left the player facing a wall.
        def fresh():
            for child in children:
                console_command(child, 'onlinepane_test')
            time.sleep(1.5)
            return [state(slot) for slot in range(1, n + 1)]
        before_keys = fresh()
        user.PostMessageW(host, 0x100, 0x53, 0x1F << 16)  # WM_KEYDOWN 'S'
        time.sleep(1.2)
        user.PostMessageW(host, 0x101, 0x53, (0x1F << 16) | 0xC0000001)  # WM_KEYUP
        time.sleep(.5)
        walked = [sum((a - b) ** 2 for a, b in zip(x[1:4], y[1:4])) ** .5 for x, y in zip(before_keys, fresh())]
        assert walked[-1] > 20 and all(m < 2 for m in walked[:-1]), ('Keys must move the keyboard view only', walked)
        if not args.no_desktop_input:
            # Real Windows focus and mouse (pad focus is not bypassed here): an activated view hands the
            # foreground back without capturing the OS mouse, and only player 1 turns from raw mouse motion.
            user.SetForegroundWindow.argtypes = [w.HWND]
            user.GetForegroundWindow.restype = w.HWND
            user.ClientToScreen.argtypes = [w.HWND, c.POINTER(w.POINT)]
            user.GetClipCursor.argtypes = [c.POINTER(w.RECT)]
            class MOUSEINPUT(c.Structure):
                _fields_ = [('dx', w.LONG), ('dy', w.LONG), ('mouseData', w.DWORD), ('dwFlags', w.DWORD),
                            ('time', w.DWORD), ('dwExtraInfo', c.c_size_t)]
            class INPUT(c.Structure):
                _fields_ = [('type', w.DWORD), ('mi', MOUSEINPUT)]  # MOUSEINPUT is the largest union member.
            def foreground(hwnd):
                # A complete Alt tap (both edges to the same window) grants foreground rights.
                user.keybd_event(0x12, 0, 0, 0); user.keybd_event(0x12, 0, 2, 0)
                user.SetForegroundWindow(hwnd)
                time.sleep(.6)
            def report(slot):
                pattern = r'ONLINEPANE_TEST .* yaw=(-?[\d.]+) input=(\d) appactive=(\d)'
                count = len(re.findall(pattern, logs(slot)))
                pane_window = next(hwnd for pid, hwnd in actual_panes if pid == children[slot - 1].pid)
                assert user.PostMessageW(pane_window, 0x8000 + 25, 0, 0)  # Read-only localhost report, no console/focus side effects.
                wait(lambda: len(re.findall(pattern, logs(slot))) > count, f'player {slot} report', children[slot - 1], 10)
                yaw, active_input, app = re.findall(pattern, logs(slot))[-1]
                return float(yaw), int(active_input), int(app)
            pane_one = next(hwnd for pid, hwnd in actual_panes if pid == children[0].pid)
            foreground(host)
            assert user.GetForegroundWindow() == host, 'Host must own the foreground'
            foreground(pane_one)  # What the old startup SetFocus did.
            assert user.GetForegroundWindow() == host, 'Activated view must return the foreground to the host'
            steady = [report(slot) for slot in range(1, n + 1)]
            assert all(r[1:] == (1, 0) for r in steady), steady  # Pads active everywhere; no OS mouse capture.
            time.sleep(1.5)
            assert abs(report(n)[0] - steady[-1][0]) < .5, 'Keyboard view drifts without input'
            clip, corner = w.RECT(), w.POINT(0, 0)
            user.GetClipCursor(c.byref(clip)); user.ClientToScreen(host, c.byref(corner))
            assert (clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top) == (corner.x, corner.y, width, height), 'Cursor not confined to host'
            yaw_before = [report(slot)[0] for slot in range(1, n + 1)]
            motion = INPUT(type=0, mi=MOUSEINPUT(dx=40, dy=0, dwFlags=1))
            for _ in range(10):
                assert user.SendInput(1, c.byref(motion), c.sizeof(INPUT)) == 1, c.get_last_error(); time.sleep(.02)
            time.sleep(.5)
            turned = [report(slot)[0] for slot in range(1, n + 1)]
            delta = [abs((b - a + 180) % 360 - 180) for a, b in zip(yaw_before, turned)]
            assert delta[-1] > 5 and all(d < .5 for d in delta[:-1]), (yaw_before, turned)  # Mouse drives the keyboard view only.
            # Alt-Tab: Alt goes down in the host (forwarded to player 1) but comes up in another window.
            # Player 1's next Enter must not become Alt+Enter (fullscreen toggle / renderer restart).
            user.GetShellWindow.restype = w.HWND
            restarts = logs(n).count(f'ONLINEPANE ready player={n}')
            user.keybd_event(0x12, 0, 0, 0); time.sleep(.3)
            user.SetForegroundWindow(user.GetShellWindow()); time.sleep(.6)
            assert user.GetForegroundWindow() != host, 'Could not switch away from the host'
            user.keybd_event(0x12, 0, 2, 0); time.sleep(.3)
            foreground(host)
            report(n); time.sleep(1)
            assert logs(n).count(f'ONLINEPANE ready player={n}') == restarts and report(n)[1] == 1, 'Stuck Alt restarted the keyboard view'
        for slot in range(1, n + 1):
            text = logs(slot)
            assert 'Unpure client' not in text and 'failed Pure Server check' not in text, (slot, text[-2000:])
            image_path = home / f'players/{slot}/{game}/screenshots/four-online.tga'
            if not image_path.exists():
                for attempt in range(30):
                    console_command(children[slot - 1], 'onlinepane_test; screenshot four-online')
                    time.sleep(1)
                    if image_path.exists(): break
            image = Image.open(image_path)
            assert image.size == (pw, ph), (slot, image.size)
            image.save(ROOT / f'build/online-{tag}-{game}-player{slot}.png')
        from PIL import ImageGrab
        origin = w.POINT(0, 0)
        user.ClientToScreen.argtypes = [w.HWND, c.POINTER(w.POINT)]
        user.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
        user.SetWindowPos(host, w.HWND(-1), 0, 0, 0, 0, 0x13)  # Fixture: keep console windows from obscuring evidence.
        time.sleep(1)
        user.ClientToScreen(host, c.byref(origin))
        composite = ImageGrab.grab(bbox=(origin.x, origin.y, origin.x + width, origin.y + height)).convert('RGB')
        composite.save(ROOT / f'build/online-{tag}-{game}-host.png')
        if n == 3:
            size = min(pw - 24, ph - 48)
            mx, my = pw + (pw - size) // 2, ph + 32
            # Oasis's gun objectives have red dynamite icons, not just terrain/letter markers.
            guns = composite.crop((int(mx + size * .8), int(my + size * .3), mx + size, int(my + size * .65)))
            assert sum(r > 90 and r > g * 1.7 and r > b * 1.7 for r, g, b in guns.get_flattened_data()) > 4, 'Missing limbo objective icons'
        for left, top in [((i % 2) * pw, (i // 2) * ph) for i in range(4 if n == 3 else n)]:
            colors = composite.crop((left + 40, top + 40, left + pw - 40, top + ph - 40)).getcolors((pw - 80) * (ph - 80))
            assert colors and len(colors) > 100, (left, top, 'Composed view is blank')
        # A per-view renderer restart cannot change the host's pane-size video.
        ready_before = logs(n).count(f'ONLINEPANE ready player={n}')
        pinned = home / f'players/{n}/{game}/screenshots/four-pin.tga'
        pinned.unlink(missing_ok=True)
        console_command(children[-1], 'set r_mode 4; set r_customwidth 800; set r_customheight 600; vid_restart')
        wait(lambda: logs(n).count(f'ONLINEPANE ready player={n}') > ready_before, 'pane restart snapshots', children[-1], 40)
        time.sleep(.5)
        console_command(children[-1], 'screenshot four-pin')
        wait(pinned.exists, 'pinned pane restart', children[-1], 20)
        assert Image.open(pinned).size == (pw, ph)
        # Actual server-provided UI: pad opens Options -> System (onOpen loads
        # the sound draft), then B backs out without applying/restarting video.
        menu_slot, menu_child = controlled + 1, children[controlled]
        menu_root = home / f'players/{menu_slot}/{game}'
        def menu_log(): return logs(menu_slot)
        def pad(button):
            return [f'gamepad_test {pads[controlled]} 0 0 0 0 {button} 1', 'wait 10', f'gamepad_test {pads[controlled]} 0 0 0 0 0 1', 'wait 10']
        # Start opens OUR match menu (not the server's); the look stick only moves its pointer, never the
        # view. B closes it; Start, Down x2, A picks SERVER MENU, which opens the server's own menu.
        menu_steps = ['set ui_s_khz 12345', f'gamepad_test {pads[controlled]} 0 0 0 0 0 1', 'wait 20', 'onlinepane_test'] + pad(16)
        menu_steps += ['wait 10', 'screenshot online-menu', f'gamepad_test {pads[controlled]} 0 0 30000 0 0 1', 'wait 40',
                       f'gamepad_test {pads[controlled]} 0 0 0 0 0 1', 'wait 10', 'onlinepane_test'] + pad(8192) + ['wait 10']
        menu_steps += pad(16) + pad(2) * 2 + pad(4096) + ['screenshot four-options-main']
        menu_steps += pad(2) * (4 if args.legacy else 3) + pad(4096)
        menu_steps += pad(2) * (5 if args.legacy else 4) + pad(4096)
        menu_steps += ['wait 30', 'ui_s_khz', 'screenshot four-options-system', 'echo OPTIONS_UI_DONE']
        menu_config = menu_root / 'four-options.cfg'
        menu_config.write_text('\n'.join(menu_steps)+'\n')
        options_image = menu_root / 'screenshots/four-options-system.tga'
        main_image = options_image.with_name('four-options-main.tga')
        options_image.unlink(missing_ok=True); main_image.unlink(missing_ok=True)
        (menu_root / 'screenshots/online-menu.tga').unlink(missing_ok=True)
        console_command(menu_child, 'exec four-options.cfg')
        wait(lambda: 'OPTIONS_UI_DONE' in menu_log(), 'online pad options/system', menu_child, 20)
        assert re.search(r'"ui_s_khz" is:"(?!12345)[\d.]+', menu_log()), menu_log()[-2500:]
        steps = menu_log().split('execing four-options.cfg')[-1]
        assert steps.count('ONLINEMENU opened items=') == 2 and 'ONLINEMENU selected item=SERVER MENU' in steps, steps[-2500:]
        yaws = re.findall(r'ONLINEPANE_TEST .* yaw=(-?[\d.]+)', steps)
        assert len(yaws) >= 2 and abs(float(yaws[0]) - float(yaws[1])) < .01, ('View turned behind the menu', yaws)
        menu_shot = menu_root / 'screenshots/online-menu.tga'
        wait(menu_shot.exists, 'online menu screenshot', menu_child, 10)
        Image.open(menu_shot).save(ROOT / f'build/online-menu-{game}.png')
        wait(options_image.exists, 'online options screenshot', menu_child, 10)
        Image.open(options_image).save(ROOT / f'build/online-options-{game}.png')
        wait(main_image.exists, 'online main screenshot', menu_child, 10)
        Image.open(main_image).save(ROOT / f'build/online-options-main-{game}.png')
        menu_config.write_text('\n'.join(pad(8192)*3+['echo OPTIONS_RETURN_DONE'])+'\n')
        console_command(menu_child, 'exec four-options.cfg')
        wait(lambda: 'OPTIONS_RETURN_DONE' in menu_log(), 'online options back', menu_child, 15)
        if args.legacy:
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as rcon:
                rcon.sendto(b'\xff'*4+b'rcon four-fixture fixture_notice', ('127.0.0.1',server_port))
            for slot, child in enumerate(children,1):
                wait(lambda: 'VISIBLE_PLAYER_CHAT' in logs(slot), 'normal server notices', child, 10)
                text=logs(slot)
                assert 'VISIBLE_REJECTION' in text and 'VISIBLE_IDENTITY_ERROR' in text, text[-2000:]
                assert 'QUIET_PRINT_ADVISORY' not in text and 'QUIET_CHAT_ADVISORY' not in text, text[-2000:]
        if args.benchmark:
            kernel.GetProcessTimes.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)] * 4
            parent_handle = kernel.OpenProcess(0x1000, False, client.pid)
            assert parent_handle
            handles.append(parent_handle)
            def cpu(handle):
                times = [w.FILETIME() for _ in range(4)]
                assert kernel.GetProcessTimes(handle, *(c.byref(t) for t in times))
                return sum((t.dwHighDateTime << 32) | t.dwLowDateTime for t in times[2:]) / 1e7
            def stats(slot, child):
                pattern = r'ONLINEFRAME_STATS mode=(\w+) rendered=(\d+) published=(\d+) skipped=(\d+) capture_ms=([\d.]+)'
                previous = len(re.findall(pattern, logs(slot)))
                console_command(child, 'onlinepane_test render')
                wait(lambda: len(re.findall(pattern, logs(slot))) > previous, 'render stats', child, 10)
                row = re.findall(pattern, logs(slot))[-1]
                return {'mode': row[0], **dict(zip(('rendered', 'published', 'skipped', 'capture_ms'), map(float, row[1:])))}
            samples = []
            for mode in (0, 1, 1, 0):  # ABBA reduces ordering/scene drift.
                for child in children: console_command(child, f'set com_maxfps 120; set r_onlineAsync {mode}')
                time.sleep(1)
                start_stats = [stats(slot, child) for slot, child in enumerate(children, 1)]
                process_handles = [parent_handle] + [child.handle for child in children]
                start_cpu = [cpu(handle) for handle in process_handles]
                start_time = time.monotonic()
                time.sleep(6)
                end_stats = [stats(slot, child) for slot, child in enumerate(children, 1)]
                seconds = time.monotonic() - start_time
                elapsed_cpu = [cpu(handle) - start for handle, start in zip(process_handles, start_cpu)]
                panes_stats = [{**{key: end[key] - start[key] for key in ('rendered', 'published', 'skipped', 'capture_ms')},
                                'mode': end['mode']} for start, end in zip(start_stats, end_stats)]
                assert all(p['published'] > seconds * 10 for p in panes_stats), panes_stats
                assert all(p['mode'] == ('async' if mode and not (n == 3 and i == 0) else 'direct') for i, p in enumerate(panes_stats)), panes_stats
                samples.append({'mode': mode, 'seconds': seconds, 'cpu_seconds': elapsed_cpu, 'panes': panes_stats})
            report = ROOT / f'build/render-benchmark-{game}-{width}x{height}.json'
            report.write_text(json.dumps({'size': [width, height], 'fps_cap': 120, 'samples': samples}, indent=2))
            print('BENCHMARK', report)
            for child in children: console_command(child, 'set r_onlineAsync 1')
        # Back opens the server cgame's original limbo in that view only; B closes it.
        for slot, child in enumerate(children, 1):
            pad = pads[slot - 1]
            if pad < 0: continue  # Keyboard/mouse was checked above; no controller belongs to this view.
            console_command(child, f'gamepad_test {pad} 0 0 0 0 32 1; wait 8; gamepad_test {pad} 0 0 0 0 0 1; wait 30; radial_test state 1')
            wait(lambda: re.search(r'RADIAL_STATE .* catch=8 ', logs(slot)), f'player {slot} limbo', child, 10)
            console_command(child, f'gamepad_test {pad} 0 0 0 0 8192 1; wait 8; gamepad_test {pad} 0 0 0 0 0 1; wait 30; radial_test state 1')
            wait(lambda: re.findall(r'RADIAL_STATE .* catch=(\d+) ', logs(slot))[-1] == '0', f'player {slot} limbo closed', child, 10)
        key_paths = [home / 'etmain/etkey'] + [home / f'players/{slot}/etmain/etkey' for slot in range(2, n + 1)]
        keys = {p: p.read_bytes() for p in key_paths}
        # A normal per-player disconnect leaves the entire group, gracefully saving profiles.
        console_command(children[1], 'disconnect')
        wait_log(parent_log, 'ONLINESPLIT stopped; returned to unified menu', client)
        assert all(p.poll() == 0 for p in children), [p.poll() for p in children]
        assert not windows(client.pid), 'Host window leaked'
        assert all(p.read_bytes() == data for p, data in keys.items()), 'Identity changed on shutdown'
        console_command(client, 'quit')
        assert client.wait(timeout=15) == 0
        assert before == {p: hashlib.sha256(p.read_bytes()).digest() for p in before}, 'Primary profile changed'
        assert (home / 'etmain/etkey').read_bytes() == KEY
        print(f'PASS {game}: {n} distinct pure clients/GUIDs, one {"2x2" if n >= 3 else "2x1"} host, pinned controllers, owner-only movement/aim/pages, online pad Options/System/Back, persistent keys, clean group leave, unchanged primary video/profile. {parent_log}')
    finally:
        if client is not None and client.poll() is None:
            client.terminate(); client.wait(timeout=10)
        for handle in handles: kernel.CloseHandle(handle)
        server.terminate(); server.wait(timeout=10)


if __name__ == '__main__':
    main()
