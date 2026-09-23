# Contributing to tinclib

## Setting up

You need:

- The [CE C/C++ Toolchain](https://github.com/CE-Programming/toolchain) (CEdev)
  with its `bin/` on `PATH`. It provides `make`, `cedev-config` and
  `cemu-autotester`.
- Python 3 for the hardware test runner and the size check. Only the standard
  library is used.
- A host C compiler (gcc or clang) for the unit tests. On Windows,
  [MSYS2](https://www.msys2.org/) provides one: `pacman -S mingw-w64-ucrt-x86_64-gcc`.
  Put `C:\msys64\ucrt64\bin` **above** `C:\Program Files\Git\mingw64\bin` in
  your `PATH`. Git ships older copies of DLLs that gcc loads, and if Git's
  folder comes first, gcc fails without printing an error. Git Bash always puts
  its own folder first, so run the unit tests from PowerShell or cmd.
- For the hardware tests only: a TI-84 Plus CE ROM image (see
  [Hardware tests](#hardware-tests)).

Clone with the protocol submodule:

```sh
git clone --recurse-submodules git@github.com:gavinhsmith/tinclib.git
git submodule update --init            # in an existing clone
```

## Repository layout

| Path | Contents |
|---|---|
| `src/tinclib.h` | The whole public API |
| `src/tinc_core.c` | The link: USB serial, framing, retries, HELLO, board resets |
| `src/tinc_wifi.c` | `tinc_isActive` |
| `src/tinc_http.c` | The request: start, poll, read, abort |
| `src/tinc_config.c` | Handoff to TINCLIBC, and the TINCHND appvar layout |
| `external/tinclib-protocol/` | The wire protocol, pinned to a tag (submodule). Never edit or copy it here: protocol changes go to that repo first |
| `examples/hello/` | GET a page and print it |
| `examples/size_min/` | Built twice for `make size-check` |
| `tests/` | Host unit tests, with stand-in CE headers and a fake board in `tests/stubs/` |
| `tests/hw/` | Hardware tests for CEmu's autotester, and their runner `run.py` |
| `project.mk` | CEdev build rules for one example or hardware test program |

## Building

```sh
make               # build every example into bin/<example>/
make hello         # or one of them
make hw-build      # build the hardware test programs
make size-check    # check the linker drops what a program doesn't use
make clean         # remove all build output
```

Keep every source file inside the repository root: on Windows, CEdev can't
build sources reached through `..`. That's why the protocol submodule lives
under `external/`.

## Updating the protocol

```sh
git -C external/tinclib-protocol fetch --tags
git -C external/tinclib-protocol checkout vX.Y.0
git add external/tinclib-protocol
```

Before 1.0 the board and the library need the exact same protocol version, so
bump it together with tinclib-firmware.

## Unit tests

```sh
make -C tests                          # build and run with cc
make -C tests CC=clang                 # any C99 compiler
make -C tests CC=gcc SANITIZE=         # without ASan/UBSan, e.g. MinGW gcc on Windows
```

The unit tests compile the library with the host compiler against the headers
in `tests/stubs/`. The serial link goes to a fake ESP board (`stubs.c`) built
on tinclib-protocol's own frame parser and encoder. It can lose replies,
reboot mid-request, stream bodies in small or empty chunks, and replay
tinclib-protocol's golden frames, which the library must match byte for byte.
`clock()` is faked too, so timeouts cost no real time.

## Hardware tests

`tests/hw/` holds small CE programs that run in
[CEmu](https://github.com/CE-Programming/CEmu)'s `cemu-autotester`. Each ends
by painting the screen green (passed) or red with the failing line, so each
has one CRC. CEmu has no ESP board, so they cover what runs without one, on
the real compiler and OS:

| Test | Checks |
|---|---|
| `canary` | Only the setup: a graphx program launches and exits. If it fails, check the ROM first |
| `nodevice` | No board: `tinc_init` gives up after `TINC_DEVICE_WAIT_MS` (so `clock()` runs), and nothing else hangs or pretends to work |
| `handoff` | `tinc_openConfig` → TINCLIBC (a stub, `stub_tinclibc/`) → back into the app's `main`, which gets `TINC_ERR_SETUP_CANCELLED` once |

### What you need

- `cemu-autotester`, found on `PATH` (CEdev ships it) or set in
  `CEMU_AUTOTESTER`.
- `AUTOTESTER_ROM`: a TI-84 Plus CE ROM image with the
  [CE C libraries](https://github.com/CE-Programming/libraries/releases)
  (`clibs.8xg`) installed. It must be one of:
  - **OS 5.4 or older.** The autotester starts programs with `Asm(prgmNAME)`.
  - **OS 5.5 or newer, jailbroken with [arTIfiCE](https://yvantt.github.io/arTIfiCE/)**,
    with its `AsmHook2` app installed.

On an arTIfiCE ROM the program is picked from the PRGM menu by its first
letter, so a test program must sort before any other program starting with
that letter (`TAHANDOF` sorts before `TINCLIBC`).

### Running

```sh
export AUTOTESTER_ROM=/path/to/ti84ce.rom
make hw-test                           # build and run all of them
make hw-test HW_ARGS="nodevice"        # or some of them
make hw-record                         # re-record the CRCs of failing screens
```

When a screen doesn't match, the runner saves it as a PNG in
`tests/hw/build/<test>/`, next to the autotester's log. The tests pass on
OS 5.3.0 and on OS 5.8.5 with arTIfiCE.

## CI

`.github/workflows/ci.yml` runs on every branch push and pull request, and
before every release. It runs the unit tests under gcc and clang with ASan and
UBSan, then a CEdev build of the examples and hardware tests, then
`make size-check`. The hardware tests need a ROM, so run them locally when you
touch the link, timing or the handoff.

Pushing a `vX.Y.Z` tag (matching `TINC_VERSION`) drafts a GitHub release with
the library and the pinned protocol files zipped up.
