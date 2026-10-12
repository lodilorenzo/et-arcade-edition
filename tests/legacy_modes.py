"""Explicit graphical integration: local bots -> ET: Legacy 2.86 pure server -> local bots.
Uses only the trusted installer extracted to reference/etlegacy-runtime; no public server.
"""
import ctypes as c
from ctypes import wintypes as w
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import shutil
import socket
import subprocess
import threading
import time
import zipfile

from online_check import wait_log
from play_game import prepare_game
from smoke_server import ROOT, play_config

KEY = b"0000001002202610091500001234"
GUID = "EA2B3709F021C51681E1C1DADF8397E9"


def console_command(process, command):
    """Send only to this test client's window, never global desktop input."""
    user = c.WinDLL("user32", use_last_error=True)
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.EnumChildWindows.argtypes = [w.HWND, c.c_void_p, w.LPARAM]
    windows = []
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

    @callback_type
    def visit(hwnd, unused):
        pid = w.DWORD()
        name = c.create_unicode_buffer(80)
        user.GetWindowThreadProcessId(hwnd, c.byref(pid))
        user.GetClassNameW(hwnd, name, len(name))
        if pid.value == process.pid and name.value == "Enemy Territory" and hwnd not in windows: windows.append(hwnd)
        return True

    @callback_type
    def top(hwnd, unused):
        visit(hwnd, 0)
        user.EnumChildWindows(hwnd, visit, 0)
        return True

    user.EnumWindows(top, 0)
    assert len(windows) == 1, windows
    hwnd = windows[0]
    for message, key, scan in ((0x100, 0xc0, 0x29), (0x101, 0xc0, 0x29)):
        assert user.PostMessageW(hwnd, message, key, scan << 16)
    assert user.PostMessageW(hwnd, 0x102, ord('`'), 0)  # Consume ET's console-key character suppression.
    for char in "/toggleconsole; " + command:
        assert user.PostMessageW(hwnd, 0x102, ord(char), 0)
    for message in (0x100, 0x101):
        assert user.PostMessageW(hwnd, message, 13, 0x1c << 16)


def main():
    modules = ROOT / "build/slice/Release"
    reference = ROOT / "reference/etlegacy-runtime"
    if not (reference / "etlded.exe").exists():
        raise SystemExit("Extract etlegacy-v2.86.0-x86.exe to reference/etlegacy-runtime using 7-Zip first.")
    root = modules / "smoke/legacy-modes"
    home = root / "client"
    command = prepare_game(modules, home)
    keyfile = home / "etmain/etkey"
    keyfile.write_bytes(KEY)  # This test owns its private profile, never the player's key.
    profiles = home / "et-rust/profiles"
    (profiles / "defaultprofile.dat").write_text('"LegacyCompatibilityCheck"')
    (profiles / "LegacyCompatibilityCheck").mkdir(exist_ok=True)
    (profiles / "LegacyCompatibilityCheck/profile.dat").write_text('"LegacyCompatibilityCheck"')
    saved = play_config(home / "et-rust")
    saved.write_text('seta name "LegacyCompatibilityCheck"\nseta et_rustVideoVersion "1"\n')
    before = saved.read_bytes()
    assert prepare_game(modules, home) == command and saved.read_bytes() == before
    assert keyfile.read_bytes() == KEY
    server_home = root / "server"
    legacy = server_home / "legacy"
    legacy.mkdir(parents=True, exist_ok=True)
    package = reference / "legacy/legacy_v2.86.0.pk3"
    shutil.copy2(package, legacy)
    shutil.copy2(reference / "legacy/qagame_mp_x86.dll", legacy)
    (home / "legacy/legacy_v2.86.0.pk3").unlink(missing_ok=True)
    http = ThreadingHTTPServer(("127.0.0.1", 0), partial(SimpleHTTPRequestHandler, directory=str(server_home)))
    threading.Thread(target=http.serve_forever, daemon=True).start()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as port:
        port.bind(("127.0.0.1", 0))
        server_port = port.getsockname()[1]
    settings = dict(net_ip="127.0.0.1", net_port=server_port, sv_pure=1, sv_maxclients=8,
                    sv_master1="", sv_master2="", sv_master3="", sv_master4="", sv_master5="",
                    logfile=2, developer=1, g_gametype=2, g_doWarmup=0, bot_enable=0, omnibot_enable=0,
                    g_bluelimbotime=1000, g_redlimbotime=1000,
                    sv_allowDownload=1, sv_wwwDownload=1, sv_wwwDlDisconnected=0,
                    sv_wwwBaseURL=f"http://127.0.0.1:{http.server_port}", rconPassword="local-fixture",
                    com_hunkMegs=256, com_recommendedSet=1, lua_modules="identity-fixture.lua")
    (legacy / "autoexec.cfg").write_text("\n".join(f'set {k} "{v}"' for k, v in settings.items()) +
                                       '\n')
    # Exercise the actual systeminfo trust boundary, not ordinary SERVERINFO.
    (legacy / "identity-fixture.lua").write_text('''function et_RunFrame(time)
    -- Oasis's map script resets wave times after autoexec; keep this fixture deterministic.
    if et.trap_Cvar_Get("g_bluelimbotime") ~= "1000" then
        et.trap_Cvar_Set("g_bluelimbotime", "1000")
        et.trap_Cvar_Set("g_redlimbotime", "1000")
    end
    local info = et.trap_GetConfigstring(et.CS_SYSTEMINFO)
    if info ~= "" and not string.find(info, "FORGED_BY_SERVER", 1, true) then
        et.trap_SetConfigstring(et.CS_SYSTEMINFO, info .. "\\\\cl_guid\\\\FORGED_BY_SERVER")
        et.G_Print("LEGACY_FIXTURE forged systeminfo\\n")
    end
end
''')
    server_log = legacy / "etconsole.log"
    server_log.unlink(missing_ok=True)
    client_log = home / "et-rust/etconsole.log"
    client_log.unlink(missing_ok=True)
    (home / "legacy/etconsole.log").unlink(missing_ok=True)
    client = None
    server = subprocess.Popen([str(reference / "etlded.exe"), "+set", "fs_basepath", str(ROOT / "runtime"),
                               "+set", "fs_homepath", str(server_home), "+set", "fs_game", "legacy",
                               "+set", "db_mode", "0", "+set", "net_enabled", "1",
                               "+set", "dedicated", "1", "+map", "oasis"], cwd=server_home,
                              creationflags=subprocess.CREATE_NEW_CONSOLE)
    try:
        wait_log(server_log, "------- Game Initialization -------", server)
        foreign = home / "legacy/profiles/LegacyCompatibilityCheck"
        foreign.mkdir(parents=True, exist_ok=True)
        (foreign / "profile.dat").write_text('"LegacyCompatibilityCheck"')
        (foreign / "etconfig.cfg").write_text('echo FOREIGN_VIDEO_PROFILE_LOADED\nseta r_mode 4\nseta r_customwidth 800\nseta r_customheight 600\nseta r_customaspect 1.333333\nseta r_fullscreen 0\nseta r_displayRefresh 0\nseta cg_drawFPS 1\n')
        extra = dict(r_mode=3, r_customwidth=640, r_customheight=480, r_customaspect=4/3,
                     r_fullscreen=0, r_displayRefresh=0, r_swapInterval=0,
                     in_mouse=0, in_gamepad=0, s_initsound=0, developer=1, ui_rustMenuTest=1,
                     logfile=2, com_maxfps=60, cl_master="127.0.0.1:9", cl_splitScreen=4, ui_matchPlayers=48, ui_oasisMap="oasis")
        for local in range(1, 5):
            extra[f"ui_oasisTeam{local}"] = 2
            extra[f"ui_oasisClass{local}"] = 2
        with (home / "et-rust/autoexec.cfg").open("a") as cfg:
            cfg.write("\n".join(f'set {k} "{v}"' for k, v in extra.items()) + "\n")
        video = ["r_mode", "r_customwidth", "r_customheight", "r_customaspect", "r_fullscreen", "r_displayRefresh"]
        match = ["ui_test script sliceStart", "wait 300"] + [f"local_join {i}" for i in range(1, 5)]
        match += ["wait 900", "map_restart 0 0", "wait 180", "omnibot_test", "split_state",
                  "ui_test script Leave", "wait 180", "ui_test pad state"]
        phases = {
            "et-rust/legacy-check.cfg": ["set developer 0", "wait 60", "ui_test pad state", "ui_test script sliceBrowse", "wait 60",
                "ui_test pad state", "ui_test pad b", "wait 5", "ui_test pad state"] + match + ["echo LOCAL_CHECK_DONE"],
            # One online view here; split online groups are covered by online_split.py.
            "et-rust/connect-check.cfg": ['set cl_splitScreen 0', 'set activeAction "echo CONNECT_CHECK_DONE"', f"connect 127.0.0.1:{server_port}"],
            "legacy/network-check.cfg": ["vid_restart", "wait 120", "vminfo"] + video + ["cg_drawFPS", "record legacy-check",
                "cmd team b 2 8 7", 'set rconPassword "local-fixture"', "rcon dumpuser 0",
                "+forward", "wait 120", "-forward", "stoprecord", "screenshot legacy-online", "echo NETWORK_CHECK_DONE"],
            "legacy/wheels-check.cfg": ["radial_test state 1", "radial_test open 1 equipment",
                "radial_test input 1 0 1", "radial_test release 1", "wait 120", "radial_test state 1",
                "radial_test open 1 voice", "radial_test input 1 0 1",
                "radial_test release 1", "wait 60", "echo WHEELS_CHECK_DONE"],
            # Back opens the server cgame's own limbo; B closes it. L3 holds the original scoreboard.
            "legacy/limbo-check.cfg": ["set in_gamepad 1", "gamepad_test 0 0 0 0 0 0 1", "wait 8", "gamepad_test 0 0 0 0 0 32 1", "wait 8",
                "gamepad_test 0 0 0 0 0 0 1", "wait 60", "radial_test state 1", "screenshot legacy-original-limbo",
                "gamepad_test 0 0 0 0 0 8192 1", "wait 8", "gamepad_test 0 0 0 0 0 0 1", "wait 20", "radial_test state 1",
                "gamepad_test 0 0 0 0 0 64 1", "wait 60", "screenshot legacy-scores", "gamepad_test 0 0 0 0 0 0 1", "wait 8",
                "set in_gamepad 0", "echo LIMBO_CHECK_DONE"],
            # Leave through our online match menu: Start, Down x3 (Leave match), A.
            "legacy/return-check.cfg": ["set in_gamepad 1", "gamepad_test 0 0 0 0 0 0 1", "wait 8", "gamepad_test 0 0 0 0 0 16 1", "wait 8", "gamepad_test 0 0 0 0 0 0 1", "wait 8", "gamepad_test 0 0 0 0 0 2 1", "wait 8", "gamepad_test 0 0 0 0 0 0 1", "wait 8", "gamepad_test 0 0 0 0 0 2 1", "wait 8", "gamepad_test 0 0 0 0 0 0 1", "wait 8", "gamepad_test 0 0 0 0 0 2 1", "wait 8", "gamepad_test 0 0 0 0 0 0 1", "wait 8", "gamepad_test 0 0 0 0 0 4096 1", "wait 8", "gamepad_test 0 0 0 0 0 0 1", "wait 8", "set in_gamepad 0", "wait 180", "echo RETURN_CHECK_DONE"],
            "et-rust/local-again.cfg": video + ["screenshot legacy-returned", "set cl_splitScreen 4"] + match + ["echo LOCAL_AGAIN_DONE"],
            "et-rust/recovery-check.cfg": ["error drop", "wait 60", "ui_test pad state", "ui_test pad b",
                "wait 10", "ui_test pad state", "echo RECOVERY_CHECK_DONE"],
        }
        for filename, commands in phases.items():
            (home / filename).write_text("\n".join(commands) + "\n")
        client = subprocess.Popen(command + ["+exec", "legacy-check.cfg"], cwd=home)
        wait_log(client_log, "LOCAL_CHECK_DONE", client)
        # Let each script finish before injecting the next. A single giant exec
        # keeps queued FS_Restart profile imports behind 'quit' and misses this bug.
        for filename, marker in (("connect-check.cfg", "CONNECT_CHECK_DONE"),
                                 ("network-check.cfg", "NETWORK_CHECK_DONE"),
                                 ("wheels-check.cfg", "WHEELS_CHECK_DONE"),
                                 ("limbo-check.cfg", "LIMBO_CHECK_DONE"),
                                 ("return-check.cfg", "PLAYMODE returned to unified menu"),
                                 ("local-again.cfg", "LOCAL_AGAIN_DONE"),
                                 ("recovery-check.cfg", 'PAD_CHECK menu="popupError"')):
            time.sleep(.5)
            if filename == "wheels-check.cfg":
                # A team change joins the next wave; don't select during PMF_RESPAWNED.
                deadline = time.monotonic() + 20
                while True:
                    console_command(client,"radial_test state 1")
                    time.sleep(.5)
                    states = re.findall(r'RADIAL_STATE health=(-?\d+) pm=(\d+) flags=(\d+) team=(\d+) catch=(\d+)',client_log.read_text(encoding="latin-1"))
                    if states and int(states[-1][0]) > 0 and states[-1][1] == "0" and not int(states[-1][2]) & 512 and states[-1][4] == "0":
                        break
                    assert time.monotonic() < deadline, (states,client_log)
            console_command(client, f"exec {filename}")
            wait_log(client_log, marker, client)
        time.sleep(1)  # Allow the popup's Back action and final menu checkpoint.
        console_command(client, "quit")
        assert client.wait(timeout=15) == 0, client_log
        text = client_log.read_text(encoding="latin-1")
        assert keyfile.read_bytes() == KEY, "Existing key changed across play modes"
        assert re.search(r"cl_guid\s+" + GUID, text), client_log
        server_text = server_log.read_text(encoding="latin-1")
        assert "LEGACY_FIXTURE forged systeminfo" in server_text, server_log
        userinfos = re.findall(r"Userinfo: ([^\n]+)", server_text)
        assert userinfos and all(re.search(r"\\cl_guid\\" + GUID, info) for info in userinfos), server_log
        for name in ("cgame_mp_x86.dll", "ui_mp_x86.dll"):
            with zipfile.ZipFile(package) as pak:
                assert (home / "legacy" / name).read_bytes() == pak.read(name)
            assert (home / "et-rust" / name).read_bytes() == (modules / name).read_bytes()
        assert (home / "legacy/legacy_v2.86.0.pk3").read_bytes() == package.read_bytes()
        for marker in ("Initializing legacy cgame ^2v2.86.0", "Initializing legacy ui ^2v2.86.0",
                       "recording to demos/legacy-check.dm_84", "PLAYMODE returned to unified menu",
                       'PAD_CHECK menu="playonline"', 'PAD_CHECK menu="popupError"',
                       "FOREIGN_VIDEO_PROFILE_LOADED", "----- CL_Shutdown -----"):
            assert marker in text, (marker, client_log)
        assert "RADIAL selected player=1 equipment bank=1" in text, client_log
        assert "RADIAL selected player=1 voice=Medic audience=0" in text, client_log
        assert re.search(r'RADIAL_STATE health=100 pm=0 .* weapon=48 ',text), client_log
        assert re.search(r'LegacyCompatibilityCheck[^\n]*Medic!',text,re.I), "Legacy server must broadcast the voice request (random caption variant)"
        catches = re.findall(r'RADIAL_STATE .* catch=(\d+) ', text.split('execing limbo-check.cfg')[-1])
        assert catches[:2] == ['8', '0'], ('Back must open original Legacy limbo; B must close it', catches)
        assert 'legacy-scores.tga' in text, client_log
        assert 'ONLINEMENU opened items=5' in text and 'ONLINEMENU selected item=LEAVE MATCH' in text, client_log
        assert 'legacy-original-limbo.tga' in text, client_log
        assert text.count("SPLITSCREEN ready") >= 2, client_log
        counts = re.findall(r"OMNIBOT_TEST count=(\d+) requested=(\d+) axis=(\d+) allies=(\d+) humans=(\d+)", text)
        assert len(counts) == 2, (counts, client_log)
        for count, requested, axis, allies, humans in counts:
            assert requested == "48" and humans == "4" and 12 <= int(count) <= 44, (counts, client_log)
            assert abs(int(axis) - int(allies)) <= 1 and int(axis) + int(allies) == int(count) + 4, counts
        assert 'PAD_CHECK menu="main"' in text.split('PAD_CHECK menu="popupError"')[-1], client_log
        values = re.findall(r'"(r_mode|r_customwidth|r_customheight|r_customaspect|r_fullscreen|r_displayRefresh)" is:"([^"^]+)\^7"', text)
        expected_video = [str(extra[name]) for name in video]
        assert values == list(zip(video, expected_video)) * 2, (values, client_log)
        assert '\"cg_drawFPS\" is:\"1^7\"' in text, "Non-video mod preferences must still load"
        for screenshot in (home / "legacy/screenshots/legacy-online.tga", home / "et-rust/screenshots/legacy-returned.tga"):
            data = screenshot.read_bytes()
            assert int.from_bytes(data[12:14], "little") == 640 and int.from_bytes(data[14:16], "little") == 480, screenshot
        assert (home / "legacy/demos/legacy-check.dm_84").stat().st_size > 8000
        errors = text.replace("ERROR: Disconnected from server", "").replace("ERROR: Testing drop error", "")
        assert not re.search(r"ERROR:|VM_Create on|Bad cgame system|Unpure client|Cannot validate pure", errors), client_log
        print(f"PASS four humans + bots (48-player target) and round restart before/after online, mod profile/video preservation, interactive error recovery, controller Back opens original Legacy limbo / B closes / L3 scores, engine-side online wheels, Legacy GUID and pure 2.86 join/download. {client_log}")
    finally:
        if client is not None and client.poll() is None:
            client.terminate()
            client.wait(timeout=10)
        server.terminate()
        server.wait(timeout=10)
        http.shutdown()
        http.server_close()


if __name__ == "__main__":
    main()
