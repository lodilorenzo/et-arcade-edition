"""Explicit graphical online-path check: master query and stock-engine pure-server join.

No public server is joined. --live-master additionally fetches the public list.
"""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import threading
import time
import zipfile

from play_online import prepare_online
from smoke_server import ROOT


def wait_log(path, marker, process):
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if path.exists() and marker in path.read_text(encoding="latin-1"):
            return
        if process.poll() is not None:
            break
        time.sleep(.1)
    raise AssertionError(f"Missing {marker}; inspect {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--modules", type=Path, default=ROOT / "build/slice/Release")
    parser.add_argument("--live-master", action="store_true")
    parser.add_argument("--udp-fallback", action="store_true", help="Force HTTP 404 and verify ET's UDP fallback")
    args = parser.parse_args()
    modules = args.modules.resolve()
    root = modules / "smoke/online-check"
    home = root / "client"
    command = prepare_online(modules / "et.exe", home)
    # Prepare is repeatable and must preserve the player's settings and stock PK3s.
    saved = home / "etmain/etconfig.cfg"
    saved.write_text('seta name "OnlineCheck"\nseta et_rustVideoVersion "1"\n')
    before = saved.read_bytes()
    assert prepare_online(modules / "et.exe", home) == command
    assert saved.read_bytes() == before
    assert "et-rust" not in " ".join(command)
    assert (home / "etmain/pak0.pk3").stat().st_size == (ROOT / "runtime/etmain/pak0.pk3").stat().st_size
    server_home = root / "server"
    prepare_online(ROOT / "runtime/etded.exe", server_home)
    base = server_home / "online-fixture"
    base.mkdir(parents=True, exist_ok=True)
    shutil.copy2(modules / "qagame_mp_x86.dll", base)
    # A trusted stock script in a separate PK3 forces a real download and fs_game switch.
    fixture = base / "online-fixture.pk3"
    with zipfile.ZipFile(ROOT / "runtime/etmain/pak0.pk3") as stock:
        with zipfile.ZipFile(fixture, "w", zipfile.ZIP_DEFLATED) as pak:
            pak.writestr("maps/oasis.script", stock.read("maps/oasis.script"))
    downloaded = home / "online-fixture/online-fixture.pk3"
    downloaded.unlink(missing_ok=True)
    http = ThreadingHTTPServer(("127.0.0.1", 0), partial(SimpleHTTPRequestHandler, directory=str(server_home)))
    threading.Thread(target=http.serve_forever, daemon=True).start()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as port:
        port.bind(("127.0.0.1", 0))
        server_port = port.getsockname()[1]
    config = {
        "net_ip": "127.0.0.1", "net_port": str(server_port), "sv_pure": "1", "sv_maxclients": "8",
        "sv_master1": "", "sv_master2": "", "sv_master3": "", "sv_master4": "",
        "sv_punkbuster": "0", "bot_enable": "0", "omnibot_enable": "0", "ui_rustSlice": "0",
        "logfile": "2", "g_gametype": "2", "g_doWarmup": "0", "com_hunkMegs": "256",
        "sv_allowDownload": "1", "sv_wwwDownload": "1", "sv_wwwDlDisconnected": "0",
        "sv_wwwBaseURL": f"http://127.0.0.1:{http.server_port}" + ("/missing" if args.udp_fallback else ""),
        "com_recommendedSet": "1",
    }
    (base / "autoexec.cfg").write_text("\n".join(f'set {k} "{v}"' for k, v in config.items()) + "\n")
    server_log = base / "etconsole.log"
    client_log = home / "etmain/etconsole.log"
    server_log.unlink(missing_ok=True)
    client_log.unlink(missing_ok=True)
    server = subprocess.Popen([str(ROOT / "runtime/etded.exe"), "+set", "fs_basepath", str(server_home),
                               "+set", "fs_homepath", str(server_home), "+set", "fs_game", "online-fixture",
                               "+set", "dedicated", "2", "+map", "oasis"], cwd=ROOT / "runtime")
    master = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    master.bind(("127.0.0.1", 0))
    master.settimeout(60)
    queries = []

    def respond():
        packet, sender = master.recvfrom(4096)
        queries.append(packet)
        master.sendto(b"\xff" * 4 + b"getserversResponse\\" + socket.inet_aton("127.0.0.1") +
                      struct.pack(">H", server_port) + b"\\EOT", sender)

    thread = threading.Thread(target=respond, daemon=True)
    thread.start()
    try:
        wait_log(server_log, "------- Game Initialization -------", server)
        with (home / "etmain/autoexec.cfg").open("a") as cfg:
            cfg.write('set r_mode "3"\nset r_fullscreen "0"\nset in_mouse "0"\nset in_gamepad "0"\nset s_initsound "0"\nset logfile "2"\nset com_maxfps "60"\n')
        commands = ["globalservers 1 84", 'set cl_master "localhost"', "globalservers 0 84",
                    f'set cl_master "127.0.0.1:{master.getsockname()[1]}"', "globalservers 0 84 %s", "wait 180"]
        if args.live_master:
            commands += ['set cl_master "master.etlegacy.com:27950"', "globalservers 0 84", "wait 600"]
        commands += [f"connect 127.0.0.1:{server_port}", "wait 900", "vminfo", "record online-check",
                     "cmd team b 2 8 7", "+forward", "wait 120", "-forward", "stoprecord",
                     "screenshot online-check", "wait 5", "quit"]
        (home / "etmain/online-check.cfg").write_text("\n".join(commands) + "\n")
        result = subprocess.run(command + ["+exec", "online-check.cfg"], cwd=home, timeout=90)
        thread.join(2)
        assert queries == [b"\xff" * 4 + b"getservers 84 %s"], queries
        text = client_log.read_text(encoding="latin-1")
        switched_log = home / "online-fixture/etconsole.log"
        if switched_log.exists():
            text += switched_log.read_text(encoding="latin-1")
        assert downloaded.read_bytes() == fixture.read_bytes(), downloaded
        with zipfile.ZipFile(ROOT / "runtime/etmain/mp_bin.pk3") as stock:
            for name in ("ui_mp_x86.dll", "cgame_mp_x86.dll"):
                assert (home / "online-fixture" / name).read_bytes() == stock.read(name)
        assert result.returncode == 0, client_log
        for marker in ("usage: globalservers 0", "Could not resolve master server: localhost",
                       "1 servers parsed (total 1)", "CL_InitCGame:", "cgame : native", "ui : native",
                       "recording to demos/online-check.dm_84", "----- CL_Shutdown -----"):
            assert marker in text, (marker, client_log)
        assert not re.search(r"ERROR:|Unpure client|Cannot validate pure|SPLITSCREEN ready", text), client_log
        demo = home / "online-fixture/demos/online-check.dm_84"
        data = demo.read_bytes()
        offset = packets = 0
        while offset + 8 <= len(data):
            sequence, length = struct.unpack_from("<ii", data, offset)
            offset += 8
            if length == -1:
                assert sequence == -1
                break
            assert 0 < length <= len(data) - offset, demo
            offset += length
            packets += 1
        assert offset == len(data) and packets > 5, (demo, packets)
        if args.live_master:
            assert any(int(n) > 1 for n in re.findall(r"(\d+) servers parsed", text)), client_log
        transport = "HTTP-to-UDP fallback" if args.udp_fallback else "HTTP"
        print(f"PASS master query, {transport} PK3 download, fs_game switch, stock DLL enforcement, original 2.60b pure-server join and snapshots. {client_log}")
    finally:
        master.close()
        http.shutdown()
        http.server_close()
        server.terminate()
        server.wait(timeout=10)


if __name__ == "__main__":
    main()
