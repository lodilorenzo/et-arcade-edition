"""Published-package check: build the zips, extract the game into a path with spaces, reject a
bad import, import the original files from runtime/etmain, and play a local Omni-bot match
through Play-ET.exe. python tests/package_check.py (opens a game window; no network play).
"""
import ctypes as c
from ctypes import wintypes as w
import hashlib
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
    work = ROOT / 'build/package-check'
    shutil.rmtree(work, ignore_errors=True)  # Only this check's own folder.
    subprocess.run([sys.executable, str(ROOT / 'tools/package.py'), '--out', str(work / 'dist'), '--version', 'check'], check=True)
    archive = work / 'dist/EnemyTerritory-ArcadeEdition-check.zip'
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        assert not [n for n in names if Path(n).name in ORIGINAL + ('etkey',)], 'Original data or identity packaged'
        z.extractall(work / 'My Games')
    with zipfile.ZipFile(work / 'dist/EnemyTerritory-ArcadeEdition-check-source.zip') as z:
        assert any(n.endswith('native/launcher.c') for n in z.namelist()) and any(n.endswith('engine/src/client/cl_main.c') for n in z.namelist())
    folder = work / 'My Games/EnemyTerritory-ArcadeEdition-check'
    launcher = folder / 'Play-ET.exe'
    log = folder / 'launcher.log'

    # A wrong folder is refused and nothing is copied.
    bad = work / 'bad install/etmain'
    bad.mkdir(parents=True)
    (bad / 'pak1.pk3').write_bytes(b'not original data')
    assert subprocess.run([str(launcher), '--import', str(bad.parent)]).returncode == 2
    text = log.read_text(encoding='utf-8')
    assert 'missing: pak0.pk3' in text and 'not the 2.60b version: pak1.pk3' in text, text
    assert not [n for n in ORIGINAL if (folder / 'etmain' / n).exists()]

    # Import from the original installation files; windowed only for this check.
    game = subprocess.Popen([str(launcher), '--import', str(ROOT / 'runtime/etmain'), '+set', 'r_fullscreen', '0',
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
        console_command(process, 'quit')
        assert game.wait(timeout=30) == 0
        text = console.read_text(encoding='latin-1')
        assert not re.search(r'menu parse error|unknown menu keyword|Couldn\'t load', text, re.I), console
        # Second start keeps the player's saved video instead of reseeding it.
        assert subprocess.run([str(launcher), '+quit'], timeout=60).returncode == 0
        assert 'exec play-video.cfg' not in (folder / 'et-rust/autoexec.cfg').read_text(encoding='latin-1')
        print(f'PASS package: no original data/identity in zip, bad import refused, verified import, stock modules extracted, '
              f'portable omnibot path with spaces, local Omni-bot match, identity in package, saved video kept. {folder}')
    finally:
        if game.poll() is None:
            process = game_process(folder)
            if process: subprocess.run(['taskkill', '/PID', str(process.pid), '/F'], capture_output=True)
            game.wait(timeout=10)


if __name__ == '__main__':
    main()
