"""Offline map-load checks for built/stock engines; --client also checks rendering."""
import argparse
import math
from pathlib import Path
import shutil
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MAPS = ("battery", "fueldump", "goldrush", "oasis", "radar", "railgun")
# Interactive target selected for this 3440x1440/120 Hz display. Test profiles
# keep their existing 640x480/60 FPS schedule and screenshot expectations.
PLAY_VIDEO_SETTINGS = {
    "r_mode":"-1", "r_customwidth":"3440", "r_customheight":"1440",
    "r_customaspect":str(3440/1440), "r_fullscreen":"1", "r_displayRefresh":"120",
    "r_swapInterval":"1", "com_maxfps":"0", "r_finish":"0",
    "r_primitives":"2", "r_picmip":"0", "r_texturebits":"32",
    "r_colorbits":"32", "r_depthbits":"24", "r_textureMode":"GL_LINEAR_MIPMAP_LINEAR",
    "r_ext_compressed_textures":"0",
    "r_ext_texture_filter_anisotropic":"1", "r_textureAnisotropy":"16",
    # Keep the original 4:3/90-degree vertical field of view on this ultrawide.
    "cg_fov":str(math.degrees(2*math.atan((3440/1440)/(4/3)))),
    "cg_drawFPS":"1", "logfile":"1",
}


def play_config(folder):
    """Mirror native default-profile selection; root settings are not the profile."""
    default=folder/'profiles/defaultprofile.dat'
    if default.exists():
        match=re.match(r'\s*"([^"\r\n]+)"',default.read_text(encoding='latin-1'))
        if match:
            name=match[1]
            if name in ('.','..') or any(c in name for c in '/\\:'):
                raise ValueError('invalid default profile path')
            return folder/'profiles'/name/'etconfig.cfg'
    return folder/'etconfig.cfg'


def has_play_video_defaults(folder):
    path=play_config(folder)
    return path.exists() and 'seta et_rustVideoVersion "1"' in path.read_text(encoding='latin-1')


def check_screenshot(path):
    data = path.read_bytes()
    if len(data) < 18:
        raise ValueError("truncated TGA header")
    width, height = struct.unpack_from("<HH", data, 12)
    pixels = data[18:]
    if (data[0], data[1], data[2], data[16]) != (0, 0, 2, 24):
        raise ValueError("expected uncompressed 24-bit screenshot")
    if (width, height) != (640, 480) or len(pixels) != width * height * 3:
        raise ValueError("unexpected screenshot dimensions or pixel count")
    if len(set(zip(pixels[0::3], pixels[1::3], pixels[2::3]))) < 16:
        raise ValueError("blank or nearly uniform framebuffer")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--modules", type=Path, default=ROOT / "build/rust/Release")
    parser.add_argument("--engine", type=Path, help="Default: built etded.exe (et.exe with --client)")
    parser.add_argument("--client", action="store_true", help="Opens a window, initializes UI/cgame/audio and captures a frame")
    parser.add_argument("--map", choices=MAPS, help="Default: check all six maps")
    parser.add_argument("--play", action="store_true", help="Interactive Oasis launcher; close the game when finished")
    parser.add_argument("--split-screen", action="store_true", help="Interactive two-controller, two-player local Oasis split-screen")
    args = parser.parse_args()
    if args.split_screen:
        args.play = True
        if args.engine:
            parser.error("--split-screen requires the built engine")
    if args.play:
        args.client = True
        if args.map not in (None, "oasis"):
            parser.error("--play supports only oasis")
    runtime = ROOT / "runtime"
    modules = args.modules.resolve()
    engine = (args.engine or modules / ("et.exe" if args.client else "etded.exe")).resolve()
    if not engine.is_file() or not (runtime / "etmain/pak0.pk3").is_file():
        raise SystemExit("Build the engine and extract etmain/*.pk3 into runtime/ first; see README.md.")
    mod = runtime / "et-rust"
    mod.mkdir(parents=True, exist_ok=True)
    # Never replace the original etmain files or downloaded executables.
    for name in ("qagame", "cgame", "ui"):
        shutil.copy2(modules / f"{name}_mp_x86.dll", mod)
    for player in range(2, 5):
        if (modules / f"cgame{player}_mp_x86.dll").exists():
            shutil.copy2(modules / f"cgame{player}_mp_x86.dll", mod)
    shutil.copytree(ROOT / "native/ui", mod / "ui", dirs_exist_ok=True)
    mode = "client" if args.client else "server"
    # "slice-play" is the existing interactive profile location; keep saved settings.
    label = "split-play" if args.split_screen else "slice-play" if args.play else "stock" if engine.parent == runtime else "built"
    print(f"Engine: {engine}", flush=True)
    for map_name in ("oasis",) if args.play else (args.map,) if args.map else MAPS:
        home = modules / "smoke" / f"{label}-{mode}" / map_name
        home.mkdir(parents=True, exist_ok=True)
        log = home / "et-rust/etconsole.log"
        screenshot = home / "et-rust/screenshots/smoke.tga"
        log.unlink(missing_ok=True)
        screenshot.unlink(missing_ok=True)
        command = [str(engine)]
        settings = {
            "fs_basepath": str(runtime), "fs_homepath": str(home),
            "fs_game": "et-rust", "dedicated": "0" if args.client else "1",
            "net_ip": "127.0.0.1", "net_port": "27970", "sv_pure": "0", "sv_maxclients": "2",
            "sv_master1": "", "sv_master2": "", "sv_master3": "", "sv_master4": "",
            "sv_allowDownload": "0", "sv_wwwDownload": "0", "sv_punkbuster": "0",
            "cl_allowDownload": "0", "cl_wwwDownload": "0", "cl_punkbuster": "0", "cl_autoupdate": "0",
            "logfile": "2", "com_hunkMegs": "256", "com_recommendedSet": "1",
            "ui_rustSlice": "1" if args.play else "0",
        }
        if args.split_screen:
            settings['cl_splitScreen'] = '1'
        if args.play:
            settings.update(bot_enable="0", sv_maxclients="8", g_gametype="2",
                            g_doWarmup="0", g_warmup="0", g_timelimit="0", g_autoFireteams="0")
        if args.client:
            settings.update(r_ignorehwgamma="1", s_initsound="1", s_khz="22")
            if not args.play:
                settings.update(r_mode="3", r_fullscreen="0")
        seed_video = args.play and not has_play_video_defaults(log.parent)
        if seed_video:
            settings.update(PLAY_VIDEO_SETTINGS)
        # Stock WinMain accepts only 1023 command-line bytes / 32 console lines.
        # Its autoexec runs before renderer/network init: use that native facility.
        config = []
        # Native ET uses profile configs, and otherwise skips the saved root file.
        if args.play and play_config(log.parent)==log.parent/'etconfig.cfg' and play_config(log.parent).exists():
            config.append('exec etconfig.cfg')
        if args.play:
            settings.update(ui_rustMonitorWidth=PLAY_VIDEO_SETTINGS['r_customwidth'],
                            ui_rustMonitorHeight=PLAY_VIDEO_SETTINGS['r_customheight'],
                            ui_rustMonitorRefresh=PLAY_VIDEO_SETTINGS['r_displayRefresh'])
        for key, value in settings.items():
            if key.startswith("fs_"):
                command.extend(("+set", key, value))
            else:
                config.append(f'set {key} "{value}"')
        log.parent.mkdir(parents=True, exist_ok=True)
        if seed_video:
            config.append('seta et_rustVideoVersion "1"')
        (log.parent / "autoexec.cfg").write_text("\n".join(config) + "\n", encoding="ascii")
        commands = [f"map {map_name}", "wait 200" if args.client else "wait 20", "vminfo"]
        if args.client:
            commands.extend(("s_info", "screenshot smoke", "wait 2"))
        commands.append("quit")
        if args.play:
            config.extend(('bind w +forward', 'bind s +back', 'bind a +moveleft', 'bind d +moveright',
                           'bind SPACE +moveup', 'bind SHIFT +sprint', 'bind CTRL +movedown',
                           'bind MOUSE1 +attack', 'bind r +reload', 'bind f +activate',
                           'bind MWHEELUP weapnext', 'bind MWHEELDOWN weapprev',
                           'bind 1 "weaponbank 1"', 'bind 2 "weaponbank 2"', 'bind 3 "weaponbank 3"',
                           'bind 4 "weaponbank 4"', 'bind 5 "weaponbank 5"', 'bind 6 "weaponbank 6"',
                           'bind 7 "weaponbank 7"'))
            (log.parent / "autoexec.cfg").write_text("\n".join(config) + "\n", encoding="ascii")
            commands = []  # Main menu first; its Start Match action hosts Oasis.
        (log.parent / "smoke-run.cfg").write_text("\n".join(commands) + "\n", encoding="ascii")
        command.extend(("+exec", "smoke-run.cfg"))
        if len(subprocess.list2cmdline(command[1:]).encode("utf-8")) > 1023:
            raise SystemExit("Test paths exceed the stock engine's command-line limit; use a shorter checkout path.")
        try:
            result = subprocess.run(command, cwd=runtime, capture_output=True, timeout=None if args.play else 120)
        except subprocess.TimeoutExpired as error:
            raise SystemExit(f"FAIL {map_name}: timeout; inspect {log}") from error
        text = log.read_text(encoding="latin-1") if log.exists() else ""
        if args.play:
            if result.returncode or "----- CL_Shutdown -----" not in text or "ERROR:" in text:
                raise SystemExit(f"FAIL interactive launcher: exit={result.returncode}; inspect {log}")
            print(f"Game closed cleanly; main-menu use does not require playing a match. Log: {log}")
            continue
        required = [f"Server: {map_name}", "------- Game Initialization -------", "==== ShutdownGame ====", "qagame : native"]
        if args.client:
            required.extend(("----- Client Initialization Complete -----", "CL_InitCGame:", "GL_RENDERER:",
                             "ui : native", "cgame : native", "samplebits", "22050 speed", "----- CL_Shutdown -----"))
        missing = [marker for marker in required if marker not in text]
        if result.returncode or missing or "ERROR:" in text:
            raise SystemExit(f"FAIL {map_name}: exit={result.returncode}, missing={missing}; inspect {log}")
        if args.client:
            try:
                check_screenshot(screenshot)
            except (OSError, ValueError) as error:
                raise SystemExit(f"FAIL {map_name}: {error}; inspect {screenshot}") from error
        print(f"PASS {map_name}: native modules, frames, clean shutdown" + (", 640x480 rendered frame and audio init" if args.client else ""), flush=True)
    print("Smoke checks only; gameplay, input, audio fidelity and network/demo parity are not established.")


if __name__ == "__main__":
    main()
