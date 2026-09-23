#!/usr/bin/env python3
"""Runs the tinclib hardware tests in CEmu's autotester.

Each directory under tests/hw/ with an autotest.json is one test: a small CE
program (main.c) plus a cemu-autotester config that launches it, presses keys
and checks CRCs of video memory. The tests paint the whole screen one color
for pass and another for fail, so each has a single right CRC. No ESP board
exists in the emulator: these cover what runs without one (no-device paths,
the TINCLIBC handoff); the protocol itself is covered by the host tests. Nothing else is used to drive or inspect the
emulator.

    python tests/hw/run.py                 # build and run every test
    python tests/hw/run.py layout widgets  # just these
    python tests/hw/run.py --record        # re-record expected CRCs of failing hashes

The developer supplies the emulator and the ROM:

    AUTOTESTER_ROM   TI-84 Plus CE ROM image (required, or --rom) with the CE
                     C libraries (clibs: graphx, fileioc, usbdrvce,
                     srldrvce) already installed.
    CEMU_AUTOTESTER  cemu-autotester executable (default: found on PATH;
                     CEdev ships one in its bin/)
    HW_LAUNCH        how programs are launched (or --launch):
                       asm       Asm(prgmNAME) from the home screen, the
                                 autotester's own launch. OS 5.4 or older.
                       artifice  OS 5.5+ jailbroken with arTIfiCE, with the
                                 AsmHook2 app installed. Each test first runs
                                 AsmHook2 (the hook it installs doesn't survive
                                 the autotester's boot), then runs the program
                                 from the PRGM menu.
                       auto      (default) artifice if the ROM has AsmHook2,
                                 else asm.

Output for each test goes to tests/hw/build/<test>/: the resolved config, the
autotester log, and for every failing hash a dump of the memory it covered,
converted to PNG.

Recording: screens in 8bpp mode (hashes over vram_8_size) are drawn only by
the test, so they have exactly one right CRC and --record replaces it. Screens
in 16bpp mode belong to the OS, which draws them differently from version to
version, so --record adds the new CRC to the list instead. Look at the PNGs
before committing a recording.
"""

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib

HW_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HW_DIR))
BUILD_DIR = os.path.join(HW_DIR, "build")

SCREEN_W = 320
SCREEN_H = 240


class SetupError(Exception):
    pass


# ---- Environment -------------------------------------------------------------


def find_autotester():
    path = os.environ.get("CEMU_AUTOTESTER") or shutil.which("cemu-autotester")
    if not path or not os.path.isfile(path):
        raise SetupError(
            "cemu-autotester not found. Put CEdev's bin/ (or your CEmu build's "
            "autotester) on PATH, or set CEMU_AUTOTESTER to the executable."
        )
    return path


def find_rom(arg):
    rom = arg or os.environ.get("AUTOTESTER_ROM")
    if not rom:
        raise SetupError(
            "No ROM given. Set AUTOTESTER_ROM (or pass --rom) to a TI-84 Plus CE "
            "ROM image with clibs installed."
        )
    if not os.path.isfile(rom):
        raise SetupError(f"ROM not found: {rom}")
    return os.path.abspath(rom)


# ---- Launching ---------------------------------------------------------------

# The autotester's "action|launch" types Asm(prgmNAME) on the home screen. OS
# 5.5 removed Asm(, so on a jailbroken ROM the step is replaced by keys that
# run the AsmHook2 app and then pick the program from the PRGM menu. Both menus
# are entered with [alpha] + the first letter, which jumps to the first entry
# starting with it, so other apps and programs don't shift the selection.

# Keys typing each letter in alpha mode, for the letters the autotester has
# key names for.
ALPHA_KEYS = {
    "A": "math", "B": "apps", "C": "prgm", "E": "sin", "F": "cos", "G": "tan",
    "N": "log", "O": "7", "P": "8", "Q": "9", "T": "4", "U": "5", "V": "6",
    "X": "sto", "Y": "1", "Z": "2",
}

ASMHOOK_APP = b"AsmHook2"


def launch_mode(arg, rom):
    mode = arg or os.environ.get("HW_LAUNCH") or "auto"
    if mode not in ("auto", "asm", "artifice"):
        raise SetupError(f"unknown launch mode '{mode}': expected auto, asm or artifice")
    if mode == "auto":
        with open(rom, "rb") as f:
            mode = "artifice" if ASMHOOK_APP in f.read() else "asm"
    return mode


def artifice_launch(target):
    letter = target[0]
    if letter not in ALPHA_KEYS:
        raise SetupError(
            f"can't select program {target} from the PRGM menu: names must start "
            f"with one of {''.join(sorted(ALPHA_KEYS))}"
        )
    keys = [
        # Install the hook: [apps], jump to "A" (AsmHook2), run it, dismiss.
        "apps", "alpha", "math", "enter", "clear",
        # [prgm], jump to the program, paste prgmNAME, run it.
        "prgm", "alpha", ALPHA_KEYS[letter], "enter", "enter",
    ]
    # The OS menus drop keys that come faster than this.
    return [step for key in keys for step in (f"key|{key}", "delay|300")]


# ---- Tests -------------------------------------------------------------------


def discover():
    return sorted(
        name
        for name in os.listdir(HW_DIR)
        if os.path.isfile(os.path.join(HW_DIR, name, "autotest.json"))
    )


def load_config(name):
    with open(os.path.join(HW_DIR, name, "autotest.json"), encoding="utf-8") as f:
        return json.load(f)


def build(names):
    make = os.environ.get("MAKE", "make")
    # Every program a test transfers (bin/<target>/NAME.8xp), e.g. the stub
    # TINCLIBC the handoff test needs too.
    targets = sorted({f.split("/")[1] for n in names for f in load_config(n)["transfer_files"]})
    print(f"Building {' '.join(targets)}")
    r = subprocess.run([make, *targets], cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stdout.write(r.stdout)
        sys.stderr.write(r.stderr)
        raise SetupError("build failed")


def program_path(name, config):
    target = config["target"]["name"]
    path = os.path.join(ROOT, "bin", f"hw_{name}", f"{target}.8xp")
    if not os.path.isfile(path):
        raise SetupError(
            f"{name}: {path} not found. Does the target name in autotest.json "
            f"match HW_NAME_{name} in project.mk?"
        )
    return path


RESULT_RE = re.compile(r"\[Test (passed|failed)!\] Hash #(\w+)")
GOT_RE = re.compile(r"\(got ([0-9A-Fa-f]+)\)")
DUMP_RE = re.compile(r"Dumped memory into (\S+)")


def run_test(name, autotester, env, launch):
    """Runs one test. Returns (passed, config, results, log) where results is a
    list of [hash id, passed, got CRC or None, dump file or None] in sequence
    order."""
    config = load_config(name)
    work = os.path.join(BUILD_DIR, name)
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(work)

    # The committed config names programs relative to the repo; make them
    # absolute, since the autotester runs in the work directory.
    program_path(name, config)
    resolved = dict(config)
    resolved["transfer_files"] = [os.path.join(ROOT, f) for f in config["transfer_files"]]
    # Launching right after a large transfer can drop the first keys the
    # launch types (seen with a 27 KB program), so wait a second first.
    steps = ["delay|1000"]
    steps += artifice_launch(config["target"]["name"]) if launch == "artifice" else ["action|launch"]
    resolved["sequence"] = [x for step in config["sequence"]
                            for x in (steps if step == "action|launch" else [step])]
    config_path = os.path.join(work, "autotest.json")
    with open(config_path, "w", encoding="utf-8") as f:
        json.dump(resolved, f, indent=2)

    # -d makes the autotester dump the memory behind each failing hash.
    r = subprocess.run(
        [autotester, "-d", config_path], cwd=work, env=env, capture_output=True, text=True
    )
    log = r.stdout + r.stderr
    with open(os.path.join(work, "autotester.log"), "w", encoding="utf-8") as f:
        f.write(log)

    results = []
    for line in log.splitlines():
        m = RESULT_RE.search(line)
        if m:
            got = GOT_RE.search(line)
            results.append([m.group(2), m.group(1) == "passed", got and got.group(1).upper().zfill(8), None])
            continue
        m = DUMP_RE.search(line)
        if m and results:
            results[-1][3] = os.path.join(work, m.group(1))

    passed = "[Autotest passed]" in log and all(ok for _, ok, _, _ in results)
    if not results or "[Error]" in log:
        passed = False
    return passed, config, results, log


# ---- Dumps -------------------------------------------------------------------


def write_png(path, width, height, rgb_rows):
    raw = b"".join(b"\x00" + bytes(row) for row in rgb_rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def dump_to_png(dump):
    """Renders a VRAM dump. 16bpp dumps are RGB565. 8bpp dumps are indices
    into graphx's default palette, where index i is the 1555 color
    i | i << 8 (bit 15 is the low bit of green)."""
    with open(dump, "rb") as f:
        data = f.read()
    rows = []
    if len(data) == SCREEN_W * SCREEN_H * 2:
        for y in range(SCREEN_H):
            row = []
            for x in range(SCREEN_W):
                v = data[2 * (y * SCREEN_W + x)] | data[2 * (y * SCREEN_W + x) + 1] << 8
                row += [(v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31]
            rows.append(row)
    elif len(data) == SCREEN_W * SCREEN_H:
        for y in range(SCREEN_H):
            row = []
            for i in data[y * SCREEN_W:(y + 1) * SCREEN_W]:
                v = i | i << 8
                row += [(v >> 10 & 31) * 255 // 31, ((v >> 5 & 31) << 1 | v >> 15) * 255 // 63,
                        (v & 31) * 255 // 31]
            rows.append(row)
    else:
        return None
    png = os.path.splitext(dump)[0] + ".png"
    write_png(png, SCREEN_W, SCREEN_H, rows)
    return png


# ---- Recording ---------------------------------------------------------------


def record(name, config, results):
    """Updates the committed config with the CRCs the failing hashes produced.
    Returns a list of messages; raises SetupError if a screen was unstable."""
    got = {}
    for hash_id, ok, crc, _ in results:
        if not ok and crc:
            got.setdefault(hash_id, set()).add(crc)

    messages = []
    for hash_id, crcs in sorted(got.items()):
        h = config["hashes"][hash_id]
        per_os = h.get("size") == "vram_16_size"
        if len(crcs) > 1 and not per_os:
            raise SetupError(
                f"{name}: hash #{hash_id} saw different screens in one run "
                f"({', '.join(sorted(crcs))}); not recording"
            )
        if per_os:
            h["expected_CRCs"] = h["expected_CRCs"] + sorted(crcs - set(h["expected_CRCs"]))
            messages.append(f"hash #{hash_id}: added {', '.join(sorted(crcs))}")
        else:
            h["expected_CRCs"] = sorted(crcs)
            messages.append(f"hash #{hash_id}: now {', '.join(sorted(crcs))}")

    if got:
        with open(os.path.join(HW_DIR, name, "autotest.json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(config, f, indent=2)
            f.write("\n")
    return messages


# ---- Main --------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tests", nargs="*", help="tests to run (default: all)")
    parser.add_argument("--rom", help="ROM image (default: $AUTOTESTER_ROM)")
    parser.add_argument("--launch", choices=["auto", "asm", "artifice"],
                        help="how to launch programs (default: $HW_LAUNCH or auto)")
    parser.add_argument("--record", action="store_true", help="re-record CRCs of failing hashes")
    parser.add_argument("--no-build", action="store_true", help="use the programs already in bin/")
    parser.add_argument("-v", "--verbose", action="store_true", help="print the autotester log of every test")
    args = parser.parse_args()

    available = discover()
    names = args.tests or available
    unknown = [n for n in names if n not in available]
    if unknown:
        print(f"Unknown test(s): {', '.join(unknown)}. Available: {', '.join(available)}", file=sys.stderr)
        return 2

    try:
        autotester = find_autotester()
        rom = find_rom(args.rom)
        env = dict(os.environ, AUTOTESTER_ROM=rom)
        launch = launch_mode(args.launch, rom)
        print(f"ROM {os.path.basename(rom)}, launching with {launch}")
        if not args.no_build:
            build(names)
    except SetupError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    failed = []
    for name in names:
        try:
            passed, config, results, log = run_test(name, autotester, env, launch)
        except SetupError as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

        status = "PASS" if passed else "FAIL"
        counts = f"{sum(ok for _, ok, _, _ in results)}/{len(results)} hashes"
        print(f"{status}  {name:<10} {counts}")
        if args.verbose or (not passed and not results):
            print(log)
        for hash_id, ok, crc, dump in results:
            if ok:
                continue
            desc = config["hashes"].get(hash_id, {}).get("description", "")
            png = dump and dump_to_png(dump)
            print(f"      hash #{hash_id} ({desc}): got {crc}" + (f"  -> {os.path.relpath(png, ROOT)}" if png else ""))

        if not passed and args.record and results:
            try:
                for msg in record(name, config, results):
                    print(f"      recorded {msg}")
            except SetupError as e:
                print(f"      {e}")
                failed.append(name)
            continue
        if not passed:
            failed.append(name)

    if failed:
        print(f"\n{len(failed)} of {len(names)} failed: {', '.join(failed)}")
        if "canary" in failed:
            print(
                "The canary failed, so the emulator setup is the likely problem rather\n"
                "than tinclib: open tests/hw/build/canary/*.png. \"Need LibLoad\" means\n"
                "the ROM doesn't have clibs installed. \"ERROR: INVALID\" means its OS\n"
                "can't launch ASM programs with Asm(: use OS 5.4 or older, or a ROM\n"
                "jailbroken with arTIfiCE that has AsmHook2 (--launch artifice)."
            )
        return 1
    if args.record:
        print("\nRecorded. Review the PNGs under tests/hw/build/ before committing.")
    else:
        print(f"\nAll {len(names)} passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
