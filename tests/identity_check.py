"""Graphical identity persistence checks in a test-owned profile (no server joined)."""
from pathlib import Path
import re
import subprocess

from play_game import prepare_game
from smoke_server import ROOT


def main():
    modules = ROOT / "build/slice/Release"
    home = modules / "smoke/identity-check"
    command = prepare_game(modules, home)
    shell = home / "et-rust"
    key = home / "etmain/etkey"
    log = shell / "etconsole.log"
    with (shell / "autoexec.cfg").open("a") as cfg:
        cfg.write('set r_mode 3\nset r_fullscreen 0\nset r_displayRefresh 0\nset in_mouse 0\nset in_gamepad 0\nset s_initsound 0\nset logfile 2\n')
    (shell / "identity-check.cfg").write_text('wait 20\nset cl_guid "TAMPERED"\ncl_guid\nuserinfo\nquit\n')

    def run(expected=None):
        log.unlink(missing_ok=True)
        result = subprocess.run(command + ["+set", "cl_guid", "SPOOFED", "+exec", "identity-check.cfg"], cwd=home, timeout=30)
        assert result.returncode == 0, log
        text = log.read_text(encoding="latin-1")
        assert "cl_guid is read only" in text, log
        if expected is None:
            expected = subprocess.check_output([str(modules / "legacy_guid_checks.exe"), str(key)], text=True).strip()
        assert f'"cl_guid" is:"{expected}^7"' in text, log
        assert "----- CL_Shutdown -----" in text, log
        return expected

    key.unlink(missing_ok=True)
    guid = run()
    original = key.read_bytes()
    assert re.fullmatch(rb"0000001002\d{18}", original) and re.fullmatch(r"[A-F0-9]{32}", guid)
    assert run(guid) == guid and key.read_bytes() == original
    key.write_bytes(original + b"extra bytes must not change the identity")
    run(guid)
    assert key.read_bytes() == original + b"extra bytes must not change the identity"
    for damaged in (b"", b"short"):
        key.write_bytes(damaged)
        run("unknown")
        assert key.read_bytes() == damaged, "Malformed existing key overwritten"
    print(f"PASS generated key format, persistent GUID, read-only/spoof resistance, longer/empty/short key preservation. {log}")


if __name__ == "__main__":
    main()
