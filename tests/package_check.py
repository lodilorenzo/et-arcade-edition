"""Published-package check: extract into a path with spaces, copy PK3 files by hand,
then play an Omni-bot match through ET-arcade-edition.exe. Also checks import and app-local DLLs.
python tests/package_check.py [--archive path/to/release.zip] (opens a game window).
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import types
import zipfile

from legacy_modes import console_command
from online_check import wait_log
from smoke_server import ROOT

ORIGINAL = ('pak0.pk3', 'pak1.pk3', 'pak2.pk3', 'mp_bin.pk3')


def game_process(folder):
    """The engine is the launcher's child: find its window by executable path."""
    user = c.WinDLL('user32'); kernel = c.WinDLL('kernel32')
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    found = []

    @c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    def visit(hwnd, unused):
        pid, name = w.DWORD(), c.create_unicode_buffer(80)
        user.GetWindowThreadProcessId(hwnd, c.byref(pid)); user.GetClassNameW(hwnd, name, 80)
        if name.value == 'Enemy Territory':
            handle = kernel.OpenProcess(0x1000, False, pid.value)
            image, size = c.create_unicode_buffer(1024), w.DWORD(1024)
            if handle and kernel.QueryFullProcessImageNameW(handle, 0, image, c.byref(size)) and \
                    Path(image.value).resolve() == (folder / 'et.exe').resolve():
                found.append(pid.value)
            if handle: kernel.CloseHandle(handle)
        return True
    user.EnumWindows(visit, 0)
    return types.SimpleNamespace(pid=found[0]) if found else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, help='Check an existing release and its matching source ZIP')
    args = parser.parse_args()
    work = ROOT / 'build/package-check'
    archive = args.archive.resolve() if args.archive else work / 'dist/EnemyTerritory-ArcadeEdition-check.zip'
    if args.archive and archive.is_relative_to(work.resolve()):
        parser.error('The archive must be outside the check\'s temporary build/package-check folder.')
    shutil.rmtree(work, ignore_errors=True)  # Only this check's own folder.
    if not args.archive:
        subprocess.run([sys.executable, str(ROOT / 'tools/package.py'), '--out', str(work / 'dist'), '--version', 'check'], check=True)
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        assert not [n for n in names if Path(n).name in ORIGINAL + ('etkey',) or Path(n).suffix.lower() in ('.pk3', '.log')], 'Original data, identity or logs packaged'
        assert {Path(n).parts[0] for n in names} == {archive.stem}, 'Unexpected ZIP root folder'
        for dll in ('vcruntime140.dll', 'msvcp140.dll'):
            assert f'{archive.stem}/{dll}' in names, f'Missing app-local runtime: {dll}'
        assert f'{archive.stem}/omnibot/omnibot_et.dll' in names
        assert f'{archive.stem}/ET-arcade-edition.exe' in names
        assert f'{archive.stem}/Play-ET.exe' not in names
        z.extractall(work / 'My Games')
    with zipfile.ZipFile(archive.with_name(f'{archive.stem}-source.zip')) as z:
        source = f'{archive.stem}-source/'
        assert z.read(source + 'LICENSE.txt') == z.read(source + 'engine/COPYING.txt'), 'Source ZIP license missing or changed'
        assert any(n.endswith('native/launcher.c') for n in z.namelist()) and any(n.endswith('engine/src/client/cl_main.c') for n in z.namelist())
    folder = work / 'My Games' / archive.stem
    launcher = folder / 'ET-arcade-edition.exe'
    log = folder / 'launcher.log'

    # A wrong folder is refused and nothing is copied.
    bad = work / 'bad install/etmain'
    bad.mkdir(parents=True)
    (bad / 'pak1.pk3').write_bytes(b'not original data')
    assert subprocess.run([str(launcher), '--import', str(bad.parent)]).returncode == 2
    text = log.read_text(encoding='utf-8')
    assert 'missing: pak0.pk3' in text and 'not the 2.60b version: pak1.pk3' in text, text
    assert not [n for n in ORIGINAL if (folder / 'etmain' / n).exists()]

    # User workflow: just copy the four PK3s into etmain and run the launcher.
    for name in ORIGINAL:
        shutil.copy2(ROOT / 'runtime/etmain' / name, folder / 'etmain' / name)
    game = subprocess.Popen([str(launcher), '+set', 'r_fullscreen', '0',
                             '+set', 'r_mode', '-1', '+set', 'r_customwidth', '1280', '+set', 'r_customheight', '720',
                             '+set', 'ui_rustMenuTest', '1', '+set', 'in_mouse', '0', '+set', 's_initsound', '0'])
    try:
        console = folder / 'et-rust/etconsole.log'
        wait_log(console, 'PLAYMODE stock ui_mp_x86.dll extracted', game)
        for name in ORIGINAL:
            assert hashlib.sha256((folder / 'etmain' / name).read_bytes()).digest() == \
                hashlib.sha256((ROOT / 'runtime/etmain' / name).read_bytes()).digest(), name
        with zipfile.ZipFile(folder / 'etmain/mp_bin.pk3') as stock:
            for name in ('cgame_mp_x86.dll', 'ui_mp_x86.dll'):
                assert (folder / 'etmain' / name).read_bytes() == stock.read(name), name
        autoexec = (folder / 'et-rust/autoexec.cfg').read_text(encoding='latin-1')
        assert f'set omnibot_path "{folder}\\omnibot"' in autoexec, autoexec
        assert re.search(r'set r_customwidth "\d+"', autoexec) and 'exec play-video.cfg' in autoexec, autoexec
        deadline = time.monotonic() + 30
        while not (process := game_process(folder)):
            assert time.monotonic() < deadline and game.poll() is None, 'Game window not found'
            time.sleep(.2)
        time.sleep(2)
        console_command(process, 'set ui_matchPlayers 8; set ui_oasisMap oasis; ui_test script sliceStart')
        wait_log(console, 'Omni-bot Loaded Successfully', game)
        deadline = time.monotonic() + 60
        while console.read_text(encoding='latin-1').count('entered the game') < 4:
            assert time.monotonic() < deadline and game.poll() is None, 'Bots did not join'
            time.sleep(.5)
        assert (folder / 'etmain/etkey').stat().st_size >= 28, 'Identity not generated inside the package'
        # Enumerate x86 modules with x86 PowerShell, not just the WOW64 host DLLs.
        powershell = Path(os.environ['WINDIR']) / 'SysWOW64/WindowsPowerShell/v1.0/powershell.exe'
        loaded = subprocess.check_output([str(powershell) if powershell.is_file() else 'powershell', '-NoProfile', '-Command',
            f'(Get-Process -Id {process.pid}).Modules | ForEach-Object {{ $_.FileName }}'], text=True)
        loaded = {Path(line.strip()).resolve() for line in loaded.splitlines() if line.strip()}
        for dll in ('vcruntime140.dll', 'msvcp140.dll'):
            assert (folder / dll).resolve() in loaded, f'Runtime did not load from the ZIP: {dll}'
        assert 'Copied the original' not in log.read_text(encoding='utf-8'), 'Manual-copy workflow must not import'
        console_command(process, 'quit')
        assert game.wait(timeout=30) == 0
        text = console.read_text(encoding='latin-1')
        assert not re.search(r'menu parse error|unknown menu keyword|Couldn\'t load', text, re.I), console
        # Verified import remains available, and saved video is not reseeded.
        assert subprocess.run([str(launcher), '--import', str(ROOT / 'runtime/etmain'), '+quit'], timeout=60).returncode == 0
        assert 'Copied the original' in log.read_text(encoding='utf-8')
        assert 'exec play-video.cfg' not in (folder / 'et-rust/autoexec.cfg').read_text(encoding='latin-1')
        print(f'PASS package: no original data/identity in ZIP, bad import refused, copy-PK3-and-run startup, verified import, '
              f'stock modules extracted, bundled VC++ DLLs loaded, local Omni-bot match, identity in package, saved video kept. {folder}')
    finally:
        if game.poll() is None:
            process = game_process(folder)
            if process: subprocess.run(['taskkill', '/PID', str(process.pid), '/F'], capture_output=True)
            game.wait(timeout=10)


if __name__ == '__main__':
    main()
