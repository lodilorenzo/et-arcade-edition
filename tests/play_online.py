"""Launch the source-built engine with stock ET menus and isolated online content."""
import argparse
from pathlib import Path
import shutil
import subprocess
import zipfile

from smoke_server import ROOT, PLAY_VIDEO_SETTINGS, has_play_video_defaults, play_config

ONLINE_SETTINGS = {
    "net_ip": "0.0.0.0", "dedicated": "0", "ui_rustSlice": "0", "cl_splitScreen": "0",
    "cl_allowDownload": "1", "cl_wwwDownload": "1", "cl_autoupdate": "0",
    "cl_punkbuster": "0", "sv_punkbuster": "0", "sv_lanForceRate": "0",
    "cl_master": "master.etlegacy.com:27950", "ui_netSource": "1",
    "rate": "25000", "snaps": "20", "cl_maxpackets": "76",
    "com_hunkMegs": "256", "com_recommendedSet": "1", "logfile": "1",
    "sv_master1": "", "sv_master2": "", "sv_master3": "", "sv_master4": "",
}


def prepare_online(engine, home):
    if not engine.is_file():
        raise SystemExit(f"Build {engine} first; see README.md.")
    assets = ROOT / "runtime/etmain"
    if not all((assets / name).is_file() for name in ("pak0.pk3", "pak1.pk3", "pak2.pk3", "mp_bin.pk3")):
        raise SystemExit("Extract the original ET assets into runtime/etmain first; see README.md.")
    base = home / "etmain"
    base.mkdir(parents=True, exist_ok=True)
    # Use one isolated base/home directory: stock Windows loads DLLs from basepath.
    # Copies (not hard links) protect the original assets from mods or later edits.
    for name in ("pak0.pk3", "pak1.pk3", "pak2.pk3", "mp_bin.pk3"):
        if not (base / name).exists():
            shutil.copy2(assets / name, base / name)
    # Bootstrap the browser; pure servers subsequently enforce their own modules.
    with zipfile.ZipFile(assets / "mp_bin.pk3") as stock:
        for name in ("ui_mp_x86.dll", "cgame_mp_x86.dll"):
            (base / name).write_bytes(stock.read(name))
    settings = dict(ONLINE_SETTINGS)
    config = []
    saved = play_config(base)
    if saved == base / "etconfig.cfg" and saved.exists():
        config.append("exec etconfig.cfg")
    if not has_play_video_defaults(base):
        settings.update(PLAY_VIDEO_SETTINGS)
        config.append('seta et_rustVideoVersion "1"')
    config.extend(f'set {key} "{value}"' for key, value in settings.items())
    (base / "autoexec.cfg").write_text("\n".join(config) + "\n", encoding="ascii")
    return [str(engine), "+set", "fs_basepath", str(home), "+set", "fs_homepath", str(home),
            "+set", "fs_game", "etmain"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=ROOT / "build/slice/Release/et.exe")
    parser.add_argument("--home", type=Path, default=ROOT / "output/online")
    args = parser.parse_args()
    home = args.home.resolve()
    command = prepare_online(args.engine.resolve(), home)
    if len(subprocess.list2cmdline(command[1:]).encode("utf-8")) > 1023:
        raise SystemExit("Paths exceed ET's command-line limit; choose a shorter --home path.")
    print("Online ET: Play Online -> Internet -> Refresh -> select server -> Connect.", flush=True)
    print("Downloads enabled: only join trusted servers; mods execute native code.", flush=True)
    print(f"Online content/settings: {home}", flush=True)
    raise SystemExit(subprocess.call(command, cwd=home))


if __name__ == "__main__":
    main()
