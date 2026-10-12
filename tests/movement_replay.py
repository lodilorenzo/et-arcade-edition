"""Compare actual C and Rust-linked server/client movement replay transcripts."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

CASES = "idle walk strafe jump crouch wall stairs ramp water fall spectator noclip freeze dead stuck timing".split()
MAPS = "battery fueldump goldrush oasis radar railgun".split()
HEADER = struct.Struct("<11I")
PREFIX = 8 + HEADER.size


def decode(data):
    if len(data) < PREFIX or data[:8] != b"ETMOVE01":
        raise ValueError("invalid replay header")
    size, player, ext, cases, modes, frames, origin, velocity, water, traces, time = HEADER.unpack_from(data, 8)
    if (cases not in (4, len(CASES)) or modes != 3 or frames != 128
            or not 0 < player < size <= 65536 or not 0 < ext < size - player
            or origin + 12 > player or velocity + 12 > player or time + 4 > player
            or water + 8 > size or traces + 20 > size
            or len(data) != PREFIX + size * cases * modes * frames):
        raise ValueError("invalid/truncated replay layout")
    return size, cases, modes, frames, origin, velocity, time


def compare(reference, candidate, label):
    layout = decode(reference)
    if decode(candidate) != layout:
        raise AssertionError(f"{label}: replay layout differs")
    if candidate == reference:
        return
    size, cases, modes, frames, origin, velocity, time = layout
    if candidate[:PREFIX] != reference[:PREFIX]:
        raise AssertionError(f"{label}: replay headers differ")
    a, b = memoryview(reference), memoryview(candidate)
    for record in range(cases * modes * frames):
        offset = PREFIX + record * size
        if a[offset:offset + size] != b[offset:offset + size]:
            byte = next(i for i in range(size) if a[offset + i] != b[offset + i])
            case = (CASES if cases == len(CASES) else CASES[1:5])[record // (modes * frames)]
            mode = (0, 8, 16)[record // frames % modes]
            def state(data):
                return (struct.unpack_from("<i", data, offset + time)[0],
                        struct.unpack_from("<3f", data, offset + origin),
                        struct.unpack_from("<3f", data, offset + velocity))
            raise AssertionError(f"{label}: {case}, fixed={mode}, frame={record % frames}, byte={byte}; "
                                 f"C(time,origin,velocity)={state(reference)} candidate={state(candidate)}")
    raise AssertionError(f"{label}: unexplained transcript mismatch")


def decode_acceleration(data):
    if len(data) < 12 or data[:8] != b"ETACCEL1":
        raise ValueError("invalid acceleration header")
    count, = struct.unpack_from("<I", data, 8)
    if not 0 < count <= 1_000_000 or len(data) != 12 + count * 12:
        raise ValueError("invalid/truncated acceleration results")
    return count


def compare_acceleration(reference, candidate, label):
    if decode_acceleration(reference) != decode_acceleration(candidate):
        raise AssertionError(f"{label}: acceleration case count differs")
    if reference != candidate:
        byte = next(i for i in range(12, len(reference)) if reference[i] != candidate[i])
        case = (byte - 12) // 12
        offset = 12 + case * 12
        raise AssertionError(f"{label}: acceleration case {case}, component={(byte - 12) % 12 // 4}; "
                             f"C bits={struct.unpack_from('<3I', reference, offset)} "
                             f"candidate bits={struct.unpack_from('<3I', candidate, offset)}")


def self_check():
    header = b"ETMOVE01" + HEADER.pack(128, 64, 16, 4, 3, 128, 4, 16, 88, 96, 0)
    valid = header + bytes(128 * 4 * 3 * 128)
    compare(valid, valid, "self-check")
    acceleration = b"ETACCEL1" + struct.pack("<I3f", 1, 1.0, 2.0, 3.0)
    compare_acceleration(acceleration, acceleration, "self-check")
    for broken in (acceleration[:-1], b"BADMAGIC" + acceleration[8:],
                   b"ETACCEL1" + struct.pack("<I", 0)):
        try:
            decode_acceleration(broken)
        except ValueError:
            pass
        else:
            raise AssertionError("validator accepted damaged acceleration output")
    changed_acceleration = acceleration[:-1] + bytes([acceleration[-1] ^ 1])
    try:
        compare_acceleration(acceleration, changed_acceleration, "self-check")
    except AssertionError:
        pass
    else:
        raise AssertionError("comparator accepted changed acceleration")
    for broken in (valid[:-1], b"BADMAGIC" + valid[8:]):
        try:
            decode(broken)
        except ValueError:
            pass
        else:
            raise AssertionError("validator accepted damaged output")
    changed = bytearray(valid)
    changed[PREFIX + 4] = 1
    try:
        compare(valid, changed, "self-check")
    except AssertionError:
        pass
    else:
        raise AssertionError("comparator accepted changed state")


def corpus(binaries, label, bsp=None, accelerate=False):
    validate = decode_acceleration if accelerate else decode
    comparator = compare_acceleration if accelerate else compare
    def run(role, variant):
        executable = binaries / f"movement_{role}_{variant}.exe"
        args = [str(executable)] + (["--accelerate"] if accelerate else [str(bsp)] if bsp else [])
        process = subprocess.run(args, capture_output=True, timeout=120)
        if process.returncode:
            raise AssertionError(f"{label}/{role}-{variant}: {process.stderr.decode(errors='replace').strip()}")
        validate(process.stdout)
        return process.stdout
    reference = run("server", "c")
    comparator(reference, run("server", "c"), f"{label}/C repeat")
    for role, variant in (("client", "c"), ("server", "rust"), ("client", "rust")):
        comparator(reference, run(role, variant), f"{label}/{role}-{variant}")
    if accelerate:
        print(f"PASS {label}: {validate(reference):,} cases per kernel, four-way exact float bits; "
              f"SHA256 {hashlib.sha256(reference).hexdigest()}")
        return 0
    _, cases, modes, frames, *_ = decode(reference)
    count = cases * modes * frames
    print(f"PASS {label}: {count:,} frames per kernel, four-way exact state/result parity, "
          f"32-command snapshot rewinds; SHA256 {hashlib.sha256(reference).hexdigest()}")
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binaries", type=Path, required=True)
    parser.add_argument("--assets", type=Path, help="optional original etmain directory containing PK3s")
    parser.add_argument("--map", choices=MAPS, help="limit optional stock-map checks")
    args = parser.parse_args()
    if args.map and not args.assets:
        parser.error("--map requires --assets")
    self_check()
    binaries = args.binaries.resolve()
    corpus(binaries, "acceleration probes", accelerate=True)
    total = corpus(binaries, "generated BSP scenarios")
    if args.assets:
        # Read-only PK3 overlay order. Only selected, fixed map names are written to a temporary directory.
        archives = sorted(args.assets.glob("*.pk3"))
        with tempfile.TemporaryDirectory(prefix="et-movement-") as temporary:
            for name in ([args.map] if args.map else MAPS):
                member, found = f"maps/{name}.bsp", None
                for archive in archives:
                    with zipfile.ZipFile(archive) as pak:
                        if member in pak.namelist():
                            info = pak.getinfo(member)
                            if not 0 < info.file_size <= 128 * 1024 * 1024:
                                raise ValueError(f"invalid BSP size in {archive}: {member}")
                            found = pak.read(member)
                if found is None:
                    raise FileNotFoundError(f"{member} not found in {args.assets}")
                bsp = Path(temporary) / f"{name}.bsp"
                bsp.write_bytes(found)
                total += corpus(binaries, name, bsp)
    print(f"PASS: {total * 4:,} frame records across four kernels, plus reference repeats and rewinds.")
    print("Shared prediction kernel/static-world collision checks, not full networking, animation or gameplay parity.")


if __name__ == "__main__":
    main()
