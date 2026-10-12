"""Compile and package the publishable Windows release: a game zip WITHOUT the original ET data,
plus its GPL source zip.

    python tools/package.py [--version 1.0.0] [--out dist] [--no-build]

Builds build/slice in Release (incremental; configured first if missing) inside the Visual Studio
x86 developer environment, then writes dist/EnemyTerritory-ArcadeEdition-<version>.zip and -source.zip.

Players extract the game zip anywhere writable and run Play-ET.exe. On first run it asks
for their original Wolfenstein: Enemy Territory 2.60b folder and copies pak0/pak1/pak2/mp_bin
into the package's etmain folder (or they copy those four files there by hand).
"""
import argparse
import datetime
from fnmatch import fnmatch
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from play_online import ONLINE_SETTINGS  # noqa: E402  Same settings as the development launcher.
from smoke_server import PLAY_VIDEO_SETTINGS  # noqa: E402

NAME = 'EnemyTerritory-ArcadeEdition'
BUILD = ROOT / 'build/slice'
MODULES = ('qagame', 'cgame', 'cgame2', 'cgame3', 'cgame4', 'ui')
# Play-ET.exe derives these from the player's monitor on first run.
MONITOR_VIDEO = {'r_mode', 'r_customwidth', 'r_customheight', 'r_customaspect', 'r_displayRefresh', 'cg_fov'}
ORIGINAL_DATA = {'pak0.pk3', 'pak1.pk3', 'pak2.pk3', 'mp_bin.pk3'}
# Never publish original game data, identities, profiles, logs or machine-specific settings.
PRIVATE = ORIGINAL_DATA | {'etkey', 'etconfig.cfg', 'autoexec.cfg', 'profile.dat', 'defaultprofile.dat', 'README.local.md'}
SOURCE_ITEMS = ('engine/COPYING.txt', 'engine/README.txt', 'engine/uncrustify.cfg', 'engine/etmain', 'engine/src',
                'native', 'rust', 'tests', 'tools', 'vendor', 'CMakeLists.txt', 'Cargo.toml', 'Cargo.lock',
                'README.md', '.gitignore', 'Build-Release.bat', 'Play-Portable.bat', 'Play-ET.bat', 'Play-ET-Online.bat')
SKIP = {'__pycache__', '.git', '.vs'}
# These apply inside the source allowlist too: a misplaced local file must not leak.
SOURCE_EXCLUDES = PRIVATE | SKIP | {
    '.hg', '.svn', '.idea', 'node_modules', 'build', 'dist', 'target', 'profiles', 'players',
    '.env', '.env.*', '*.local', '*.log', '*.stackdump', '*.pyc', '*.pyo',
    '*.exe', '*.dll', '*.lib', '*.obj', '*.pdb', '*.ilk', '*.pch',
    '*.pk3', '*.zip', '*.7z', '*.rar', '*.dem', '*.dm_*',
    '*.key', '*.pem', '*.p12', '*.pfx', '*.jks', '*.keystore', '*.db', '*.sqlite*',
}


README = """Enemy Territory: Arcade Edition ({version})
==========================================

A source-built Wolfenstein: Enemy Territory (protocol 84, 2.60b compatible) with one,
two, three or four player split-screen, controller support, Omni-bot matches on the six
original maps, and online play on normal Internet servers.

This package does NOT contain the original game data. You need your own copy of
Wolfenstein: Enemy Territory 2.60b (freeware).

FIRST START
 1. Extract this folder somewhere writable, e.g. C:\\Games\\Enemy Territory
    (not Program Files; use a path with plain letters, digits and spaces).
 2. Run Play-ET.exe.
 3. When asked, select your original Enemy Territory folder (the one containing
    "etmain"). The launcher checks and copies these four files into this
    package's etmain folder:
        pak0.pk3  pak1.pk3  pak2.pk3  mp_bin.pk3
    You can also copy them there yourself before starting.
 The first start uses your desktop resolution and refresh rate; change them in
 Options. Later starts keep your settings.

PLAYING
 - START MATCH: original objectives on Battery, Fuel Dump, Gold Rush, Oasis, Radar
   or Rail Gun with 1, 2, 3 or 4 local players (split-screen) and Omni-bot filling teams.
 - PLAY ONLINE: Internet/LAN server browser. With 2, 3 or 4 players selected, every local
   player joins the chosen server in its own view; only servers with enough free slots
   are listed. Password-protected servers are hidden.
 - Players & controllers: assign keyboard/mouse to any player, with one controller for
   each remaining player, or choose Controllers only. Options > Gamepad settings lists buttons.
 - Three players: 2x2 layout, with a live full-playfield map in the fourth quadrant.
   The map shows player one's server-provided intelligence; no extra online client is joined.

ONLINE SAFETY
 Servers can make you download mods, and mods run native code: only join servers you
 trust. PunkBuster is not supported. Some servers limit how many players may connect
 from one address, or require a particular client, and may refuse split-screen players.

YOUR DATA
 Everything stays in this folder: settings and profiles (et-rust, etmain), downloaded
 server content (etmain and mod folders), split-screen player profiles (players).
 etmain\\etkey is your online identity (GUID) - back it up to keep it. To uninstall,
 delete the folder.

TROUBLESHOOTING
 Launcher messages are also written to launcher.log; game consoles to etconsole.log in
 et-rust or the mod folder. Requires Windows 8.1 or newer and an OpenGL driver.

LICENSES
 See LICENSE.txt (GPL v3 with id Software's additional terms) and THIRD-PARTY-NOTICES.txt.
 The complete corresponding source code is published as {source}.
"""

NOTICES = """Third-party notices
===================

Engine and game code
 Based on the Wolfenstein: Enemy Territory GPL source release (id Software,
 https://github.com/id-Software/Enemy-Territory), GPL v3 with additional terms -
 see LICENSE.txt. Includes selected code adapted from ET: Legacy 2.86
 (https://www.etlegacy.com, GPL v3) and new C/C++/Rust code under the same license.
 Complete corresponding source: {source}.

Original game data (NOT included)
 pak0.pk3, pak1.pk3, pak2.pk3 and mp_bin.pk3 belong to id Software / Splash Damage /
 Activision and are subject to the original Enemy Territory license. Players supply
 them from their own installation.

Omni-bot (folder "omnibot")
 Omni-bot runtime (omnibot_et.dll, scripts and navigation) as distributed by the
 ET: Legacy mirror (https://mirror.etlegacy.com/omnibot/). Upstream source:
 https://github.com/omnibot/omnibot. The upstream runtime archive does not state its
 license terms; its readme and changelog are included unchanged. The game-side
 Omni-bot interface headers are GPL and included in the source archive (vendor/omnibot).
"""


def config(settings):
    return ''.join(f'set {key} "{value}"\n' for key, value in settings.items())


def copy_tree(source, target, skip=()):
    for path in sorted(source.rglob('*')):
        if any(part in SKIP for part in path.parts) or path.name in skip or path.suffix == '.log':
            continue
        destination = target / path.relative_to(source)
        if path.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)


def write_zip(folder, archive):
    archive.unlink(missing_ok=True)
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path in sorted(folder.rglob('*')):
            output.write(path, Path(folder.name) / path.relative_to(folder))


def stage_game(build, stage, source_name, version):
    missing = [name for name in ['et.exe', 'Play-ET.exe'] + [f'{m}_mp_x86.dll' for m in MODULES] if not (build / name).is_file()]
    if missing:
        raise SystemExit(f'Build {build} first (missing {", ".join(missing)}); see README.md.')
    for name in ('runtime/omnibot/omnibot_et.dll', 'native/ui/slice.menu'):
        if not (ROOT / name).is_file():
            raise SystemExit(f'Missing {name}.')
    stage.mkdir(parents=True)
    for name in ('et.exe', 'Play-ET.exe'):
        shutil.copy2(build / name, stage)
    shell = stage / 'et-rust'
    shell.mkdir()
    for module in MODULES:
        shutil.copy2(build / f'{module}_mp_x86.dll', shell)
    copy_tree(ROOT / 'native/ui', shell / 'ui')
    copy_tree(ROOT / 'runtime/omnibot', stage / 'omnibot', skip={'README.local.md'})
    local = dict(ONLINE_SETTINGS, ui_rustSlice='1')
    del local['cl_splitScreen']  # The local menu owns the shared split-screen player count.
    (shell / 'play-settings.cfg').write_text(config(local), encoding='ascii')
    base = stage / 'etmain'
    base.mkdir()
    (base / 'play-settings.cfg').write_text(config(ONLINE_SETTINGS), encoding='ascii')
    video = {key: value for key, value in PLAY_VIDEO_SETTINGS.items() if key not in MONITOR_VIDEO}
    (base / 'play-video.cfg').write_text(config(video), encoding='ascii')
    (base / 'COPY ORIGINAL GAME FILES HERE.txt').write_text(
        'Copy pak0.pk3, pak1.pk3, pak2.pk3 and mp_bin.pk3 from the etmain folder of your\r\n'
        'Wolfenstein: Enemy Territory 2.60b installation into this folder,\r\n'
        'or run Play-ET.exe and select your Enemy Territory folder.\r\n', encoding='ascii')
    (stage / 'README.txt').write_text(README.format(version=version, source=source_name).replace('\n', '\r\n'), encoding='ascii')
    (stage / 'THIRD-PARTY-NOTICES.txt').write_text(NOTICES.format(source=source_name).replace('\n', '\r\n'), encoding='ascii')
    shutil.copy2(ROOT / 'engine/COPYING.txt', stage / 'LICENSE.txt')
    # Game data of any kind (.pk3) must come from the player's own copy, never from this package.
    leaked = [p for p in stage.rglob('*') if p.name in PRIVATE or p.suffix.lower() in ('.log', '.pk3') or p.name == 'profiles']
    if leaked:
        raise SystemExit(f'Refusing to package private/original files: {leaked}')


def stage_source(stage, root=ROOT):
    """Copy current source files, not Git history or local/generated data."""
    root = root.resolve()
    for item in SOURCE_ITEMS:
        source = root / item
        if not source.exists():
            raise SystemExit(f'Missing source item {item}.')
        if source.is_symlink() or not source.resolve().is_relative_to(root):
            raise SystemExit(f'Refusing linked source item {item}.')
        paths = sorted(source.rglob('*')) if source.is_dir() else (source,)
        for path in paths:
            relative = path.relative_to(root)
            if any(fnmatch(part.lower(), pattern.lower())
                   for part in relative.parts for pattern in SOURCE_EXCLUDES):
                continue
            if path.is_symlink() or not path.resolve().is_relative_to(root):
                raise SystemExit(f'Refusing linked source file {relative}.')
            if path.is_file():
                if path.stat().st_size >= 100 * 2**20:
                    raise SystemExit(f'Source file exceeds GitHub\'s 100 MiB limit: {relative}.')
                destination = stage / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, destination)


def developer_shell():
    """Command prefix that enters the Visual Studio x86 developer environment."""
    vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    if vswhere.is_file():
        found = subprocess.run([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                                '-property', 'installationPath'], capture_output=True, text=True).stdout.strip()
        devcmd = Path(found) / 'Common7/Tools/VsDevCmd.bat' if found else None
        if devcmd and devcmd.is_file():
            return f'call "{devcmd}" -arch=x86 -host_arch=x64 >nul 2>&1 && '
    if shutil.which('cmake'):
        return ''  # Already inside a developer shell.
    raise SystemExit('Visual Studio C++ build tools (or cmake on PATH) not found; see README.md.')


def build():
    shell = developer_shell()
    steps = []
    if not (BUILD / 'CMakeCache.txt').is_file():
        steps.append(f'cmake -S "{ROOT}" -B "{BUILD}" -A Win32 -DET_RUST_RANDOM=ON -DET_RUST_SHARED=ON')
    steps.append(f'cmake --build "{BUILD}" --config Release --parallel')
    for step in steps:
        print(f'> {step}', flush=True)
        if subprocess.call(f'{shell}{step}', shell=True, cwd=ROOT):
            raise SystemExit('Build failed.')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true', help='Package the existing build/slice/Release without compiling')
    parser.add_argument('--out', type=Path, default=ROOT / 'dist')
    parser.add_argument('--version', default=datetime.date.today().strftime('%Y.%m.%d'))
    args = parser.parse_args()
    if not args.version.replace('.', '').replace('-', '').isalnum():
        raise SystemExit('Use a simple version such as 2026.10.10.')
    if not args.no_build:
        build()
    out = args.out.resolve()
    game_name, source_name = f'{NAME}-{args.version}', f'{NAME}-{args.version}-source'
    out.mkdir(parents=True, exist_ok=True)
    staging = out / 'staging'
    shutil.rmtree(staging, ignore_errors=True)  # Only this script's own staging folder.
    stage_game(BUILD / 'Release', staging / game_name, f'{source_name}.zip', args.version)
    stage_source(staging / source_name)
    for name in (game_name, source_name):
        write_zip(staging / name, out / f'{name}.zip')
        print(f'{out / f"{name}.zip"}  {(out / f"{name}.zip").stat().st_size / 2**20:.1f} MiB', flush=True)
    shutil.rmtree(staging)
    print('Players: extract, run Play-ET.exe, select their original Enemy Territory 2.60b folder.')


if __name__ == '__main__':
    main()
