# AGENTS.md — tinclib

## What this repo is

The CE-side C library (`tinclib.h` + `src/`) that TI-84 Plus CE programs
link against to get Wi-Fi/HTTP(S) connectivity through an ESP8266 board.
This is the library apps `#include`; it is **not** the config app
(`tinclib-config`) and **not** the firmware (`tinclib-firmware`) — keep
those concerns out of this repo.

Consumes `tinclib-protocol` as a pinned dependency: a git submodule at
`external/tinclib-protocol`, currently on tag **`v0.6`** (`fde44be`). Upstream tags
`vMAJOR.MINOR` from 0.5 on; `v0.1.0`-`v0.4.0` are aliases for older pins. It lives inside
the repo root because CEdev on Windows can't build sources reached through
`..`. **Never fork or hand-copy `protocol.h`/`crc16.c`.** If something needs
a protocol change, that goes in `tinclib-protocol` first; then bump the
submodule here (pre-1.0, in step with tinclib-firmware: HELLO needs an exact
MAJOR.MINOR match). The release zip bundles the pinned protocol files
automatically. That's packaging, not a fork.

## Where things stand

- **v0.1.0 merged to `main`** (PR #1, branch `phase-1`). It implements
  everything protocol 0.1 allows: HELLO, STATUS, REQ_BEGIN (GET,
  `http://` only), REQ_STATUS, BODY_READ, REQ_ABORT, plus the TINCLIBC
  handoff.
- **v0.2.0 (branch `phase-2`): protocol v0.2.0.** 0.2 only adds Wi-Fi admin
  features (hidden networks in `WIFI_SET`/`WIFI_LIST`, and an ESP-side
  Wi-Fi lock: the STATUS `flags` byte and `ERR_LOCKED`). The library sends
  none of the admin commands, so the changes are the version, the
  `ERR_LOCKED` string, and tests that a locked board still counts as
  online. No API change: the lock isn't exposed, because apps don't need it
  (TINCLIBC reads STATUS itself). Released as tinclib v0.2.0.
- **v0.3.0: protocol v0.3.0.** Again Wi-Fi admin only: the firmware sets
  the number of Wi-Fi slots (HELLO appends `wifi_slots`), and `WIFI_LIST`
  is replaced by the per-slot `WIFI_GET`. The library ignores the extra
  HELLO byte (payloads are append-only), so the changes are the version and
  the fake board's 11-byte HELLO. No API change.
- **v0.4.0 (branch `phase-4`): protocol v0.4.0, HTTPS.** The library
  already passed `https://` URLs through and mapped `TLS` to
  `TINC_SECURING`, so the changes are the version, `tinc_errString` for
  `ERR_TLS`/`ERR_CERT`/`ERR_TIME`/`ERR_REDIRECT_DOWNGRADE`, and tests (the
  fake board reports `TLS` for https and appends `err_detail` to
  REQ_STATUS). One API addition: `tinc_errDetail()` returns the TLS
  reason (`err_detail`, a `TINC_TLSR_*` code) for `ERR_TLS`/`ERR_CERT`,
  taken from REQ_STATUS or a BODY_READ error reply; 0 otherwise.
- Upstream moved the `v0.2.0` protocol tag (from `9c21d6e` to `1c008bc`)
  after tinclib v0.2.0 shipped. The files are identical; only the history
  was rewritten.
- **v0.4.1 (branch `win-serial-patch`): PC host fix.** With the calculator
  plugged straight into a PC, `tinc_init()` gave up after 600 ms, before
  Windows' usbser was loaded and the PC app had the port open.
  `tinc_init()` now retries HELLO on `NO_REPLY` until `TINC_DEVICE_WAIT_MS`,
  and a repeat `tinc_init()` keeps USB up instead of re-enumerating (which
  made COM9 vanish on every retry). While the calculator is idle nothing
  answers USB, so the PC's open blocks until the next tinclib call. The
  2 s window is enough, first plug-in included (tested on hardware).
- **v0.5.0 (branch `phase-5`): protocol v0.5, uploads and headers.**
  Methods GET/POST/PUT/DELETE/PATCH/HEAD (`tinc_method_t` values equal the
  wire's `TINC_METHOD_*`). A body goes out by `BODY_WRITE`, streamed from
  the app's pointer by `tinc_poll()`, as much as the ESP takes per write.
  As the protocol intends, the write loop is also the connect poll (no
  REQ_STATUS until the upload is done), so an upload reports
  `TINC_CONNECTING`, then the new `TINC_SENDING` once bytes are taken, and
  never `TINC_SECURING`. `RESPONDED` ends the upload early and the response
  is read as usual. New `tinc_header(name, out, cap)` over `HDR_GET`: pages
  through long values, returns the full length or -1. Its reply lands in
  the shared frame buffer, so a pending body chunk is fetched again (same
  offset re-delivers it). Not used: `INFO` (display only, TINCLIBC's job),
  `HDR_GET`'s `index` (always 0) and `TRUNC` flag.
- **v0.6.0 (branch `phase-6`): protocol v0.6, TLS stall allowance.** One
  TLS crypto step can stall the ESP for several hundred ms, so for an
  https request every reply timeout is `TINC_REPLY_TIMEOUT_TLS_MS` (1000)
  instead of 200, from REQ_BEGIN until a REQ_STATUS state >= `SENDING` or a
  BODY_WRITE that took bytes (`tinc_g.tls_wait`). A `tinc_abort()` in that
  window keeps the long timeout. http never uses it. URL scheme match is
  lowercase `https:` only.
- **Real firmware, from a PC:** `make pc-link` builds `tools/pc_link.c`
  (the real `src/`, with srldrvce swapped for Win32 serial) and runs it
  against a board on a COM port. The board needs firmware on the same
  protocol version as the library (now 0.6), or HELLO fails with
  `ERR_VERSION`.
- **Hardware-tested as of v0.6.0 (2026-09-23):** the calculator straight
  into a PC (tinclib-firmware's `pc` target, COM9), a calculator with a
  board, and full requests through real firmware all work. srldrvce only
  drives CDC, FTDI and PL2303 bridges (not CP210x or CH340), so a board
  needs one of those, or a native-USB chip, to talk to a calculator.
- The `INSECURE` request flag, chunked uploads (`TINC_LEN_UNKNOWN`) and the
  BOOT event are waiting on the protocol. When they land there, add them
  here.
- Cross-repo follow-up: tinclib-config must **exit** when done instead of
  relaunching `return_to` (see Handoff below), and must match the TINCHND
  layout in `src/tinc_config.c`.

## Source layout

| File | What |
|---|---|
| `src/tinclib.h` | The whole public API, `TINC_VERSION` (built from the protocol's MAJOR.MINOR + `TINC_VERSION_PATCH`), tunables (`TINC_RX_BUF_SIZE`, `TINC_DEVICE_WAIT_MS`, `TINC_WIFI_WAIT_MS`) |
| `src/tinc_internal.h` | The single state struct `tinc_g`, `tinc_xfer()` and helpers |
| `src/tinc_core.c` | USB/srldrvce, frames streamed out piece by piece (no TX buffer), stop-and-wait with same-SEQ retries, HELLO, re-handshake on `ERR_NO_HELLO`, `tinc_errString` |
| `src/tinc_wifi.c` | `tinc_isActive` |
| `src/tinc_http.c` | The one request: begin, poll, read, abort |
| `src/tinc_config.c` | `tinc_openConfig`, TINCHND layout, setup-result pickup |
| `src/tinclib.hpp` | C++ bindings, header only: the same calls in `namespace tinc`, enum classes, `tinc::Session` (init/shutdown by scope) |

## Toolchain

- **CE C/C++ Toolchain (CEdev)**, targeting the eZ80 in the TI-84 Plus CE.
- Serial transport is **`srldrvce`** (built on `usbdrvce`), used in host
  mode — the calculator is the USB host, the ESP board's bridge chip is
  the device.
- `usb_HandleEvents()` must be called regularly for the USB stack to work;
  library functions that touch the link (`tinc_poll()` etc.) should pump
  this internally so an app that only calls the high-level API still works
  without explicitly managing USB events itself. (Done: every wait in
  `tinc_core.c` pumps it.)
- The USB handler also accepts a PC acting as USB host
  (`USB_HOST_CONFIGURE_EVENT`), so a PC-side fake board can stand in for
  the ESP during development.
- All timing uses `clock()` (`CLOCKS_PER_SEC` = 32768 on CE) through
  `tinc_elapsed()`. It is verified to run on the real OS by
  `tests/hw/nodevice`. Host tests replace it with a fake clock
  (`-Dclock=tinc_fake_clock`).

## Distribution model: static, not LibLoad

LibLoad (a shared `.8xv` library apps link against at runtime) was
**explicitly rejected** — it requires the library to be written in raw
eZ80 assembly, which isn't realistic here, and LibLoad's documentation
situation makes it a bad bet regardless. **Do not propose LibLoad.** The
model is:
- Static linking: `tinclib.h` + a `src/` directory of `.c` files that a
  consuming program's build pulls in directly. Decided: the release zip
  (`tinclib/src/`, protocol files included) is unzipped into the app's
  `lib/` and added with `EXTRA_C_SOURCES = $(wildcard lib/tinclib/src/*.c)`
  (see README).
- Rely on the linker discarding unused functions/data so a program that
  only uses a few features doesn't pay for the whole library. **Verified**
  with CEdev v15 (LTO, `-Oz`): `examples/size_min` built with only
  `tinc_init`/`tinc_isActive` is 4,135 bytes, and the same program using
  the whole API (POST and `tinc_header` included) is 10,030 bytes. `make size-check` (in CI) fails if the gap
  drops below 2,000 bytes.
- Split source by feature (core/framing, Wi-Fi status, HTTP request
  handling) so optional pieces stay separable even without perfect dead-code
  elimination.
- State lives in a small number of static buffers/structs sized by
  compile-time defines (`TINC_RX_BUF_SIZE`, `TINC_MAX_SOCKETS`-equivalent,
  etc.) — **no `malloc`**. This is a RAM-constrained target; dynamic
  allocation was never on the table. Currently there is one frame buffer
  (`TINC_FRAME_BUF(TINC_RX_BUF_SIZE)`, default 264 bytes), which also holds
  the pre-fetched body chunk, plus srldrvce's 256-byte ring buffer. The
  shared buffer is marked `ponytail:` in `tinc_internal.h`. Uploads finish
  before the body starts, and `tinc_header()` re-fetches a pending chunk,
  so it hasn't needed splitting.

## API shape — read this before changing any function signature

The API went through several rounds of simplification during design. The
current shape is deliberately minimal. **Do not reintroduce the earlier,
more complex version** (a `tinc_req_t` handle struct with
`ReqStart/ReqPoll/ReqEnd/ReqInfo`, separate status-refresh calls, a
`tinc_get()` convenience wrapper, separate handoff/needs-setup calls) —
that version was walked back on purpose once the one-request-at-a-time
design made most of it unnecessary scaffolding.

### Naming convention
`tinc_camelCase()` for functions (matches toolchain conventions like
`gfx_SetColor`, `kb_Scan` in spirit, but the user's explicit choice is
camelCase after the `tinc_` prefix — e.g. `tinc_isActive`, `tinc_httpStatus`,
not `tinc_IsActive` or `tinc_is_active`). Types: `tinc_snake_case_t`.
Constants: `TINC_SCREAMING_CASE`.

### Current shape (as implemented in v0.6.0; `src/tinclib.h` is authoritative)

```c
typedef struct {
    const char *appName;   /* this program, e.g. "MINIBRWS"; needed for the config handoff */
    uint8_t     timeoutS;  /* default per-request timeout */
    uint8_t     flags;     /* TINC_CF_ASCII: default ASCII transcoding on */
} tinc_config_t;

tinc_err_t   tinc_init(const tinc_config_t *cfg);   /* NULL = defaults */
bool         tinc_isActive(tinc_mode_t mode);        /* TINC_WIFI (more modes later, e.g. TINC_SECURE) */
void         tinc_shutdown(void);

typedef struct {
    tinc_method_t method;
    const char   *url;
    const char   *headers;   /* raw "Name: value\r\n..." or NULL */
    const void   *body;      /* must stay valid until state reaches TINC_BODY */
    uint16_t      bodyLen;
} tinc_request_t;

tinc_err_t   tinc_request(const tinc_request_t *req);  /* starts, returns immediately */
tinc_state_t tinc_poll(void);   /* TINC_CONNECTING, TINC_SECURING, TINC_SENDING, TINC_WAITING, TINC_BODY, TINC_DONE, TINC_ERROR */
int16_t      tinc_read(void *buf, uint16_t cap);        /* bytes read, 0 = nothing yet */
uint16_t     tinc_httpStatus(void);
const char  *tinc_contentType(void);
int16_t      tinc_header(const char *name, char *out, uint16_t cap);  /* full length, -1 = none */
tinc_err_t   tinc_error(void);
uint8_t      tinc_errDetail(void);                  /* TINC_TLSR_* for ERR_TLS/ERR_CERT, else 0 */
const char  *tinc_errString(tinc_err_t e);
void         tinc_abort(void);

tinc_err_t   tinc_openConfig(const char *hint);   /* handoff to TINCLIBC; returns only on failure */
```

Implementation details worth knowing:
- `tinc_err_t` is `uint8_t`. It reuses protocol.h's wire codes (`TINC_OK`,
  `TINC_ERR_DNS`, ...) unchanged, and adds library-only codes from `0x80`:
  `NO_DEVICE`, `NO_REPLY`, `ESP_RESET`, `NOT_INIT`, `UNSUPPORTED_METHOD`,
  `SETUP_CANCELLED`, `SETUP_FAILED`, `NO_CONFIG_APP`. Don't duplicate a
  wire code under a new name.
- `tinc_state_t` also has `TINC_IDLE` (no request yet, or after
  `tinc_abort()`). `TINC_DONE` is reported only once EOF has arrived
  **and** the app has read the last chunk.
- The protocol's Wi-Fi states are `NO_CREDS`/`CONNECTING`/`CONNECTED`/`FAILED`.
  The "NO_NETWORKS/UNREACHABLE" wording below maps to `NO_CREDS`/`FAILED`.
- A board reset mid-request (`ERR_NO_HELLO`) triggers an automatic
  re-HELLO. The request fails with `TINC_ERR_ESP_RESET` and is never
  resent. Commands outside a request are resent once.

Key properties to preserve:
- **One request at a time.** No request handle struct passed around by the
  caller — the library owns the single in-flight request.
- **`tinc_poll()` is the only pump.** It advances the wire state machine,
  streams the POST body within the ESP's advertised window, and
  pre-fetches the next `BODY_READ` chunk into an internal buffer.
  `tinc_read()` is just a copy out of that internal buffer, not a wire
  operation itself.
- **No explicit "end/release" call.** A request releases itself on
  `DONE`, `ERROR`, or `tinc_abort()`. (This is a deliberate simplification
  from an earlier design that had `tinc_req_end()`.)
- **`tinc_httpStatus()`/`tinc_contentType()` are valid once state reaches
  `TINC_BODY`** — they come from the same `REQ_STATUS` reply that carries
  the state, no extra round trip. Don't add a separate "fetch response
  info" call; that was folded away intentionally.
- **The request body pointer must stay valid until `TINC_BODY`.** The
  library streams it out during `tinc_poll()`; it does not copy it
  internally. Document this loudly in the header — it's a sharp edge.
- **`tinc_isActive()` blocks briefly (bounded, ~8s) only while Wi-Fi state
  is `CONNECTING`.** It should return `false` promptly for `NO_NETWORKS`/
  `UNREACHABLE`/timeout, not hang indefinitely. This bounded-wait behavior
  is intentional — it avoids a cold-boot false negative that would
  otherwise send every app straight into a needless config handoff.

### Not yet in the API (known gap, don't silently add without flagging)
- No way to read the nth copy of a repeated header (`HDR_GET`'s `index`),
  to tell a real miss from a `TRUNC` false negative, or to get the board's
  firmware version (`INFO`). Add them when an app needs them.
- No upload progress (bytes taken so far) beyond `TINC_SENDING`.

## Handoff to TINCLIBC (config app)

- `tinc_openConfig(hint)` does not return on success — it launches
  TINCLIBC with `os_RunPrgm` and this program is unloaded. (It returns
  `tinc_err_t` on failure, e.g. TINCLIBC not installed.)
- **The calling app's state is lost on handoff.** Any app calling
  `tinc_openConfig()` must save what it needs (to its own appvar) *before*
  calling it, and check for a return on its next startup. This library
  cannot do that saving on the app's behalf — document this prominently,
  it's the single sharpest edge in the whole API.
- The app comes back through `os_RunPrgm`'s **return callback**, not a
  relaunch by TINCLIBC: `tinc_openConfig()` starts TINCLIBC with a callback
  that calls the app's `main()` again, and TINCLIBC just **exits** when
  done. `tinc_config_t.appName` is still required and written to
  `TINCHND.return_to`, for display only.
- The app's next `tinc_init()` reflects the outcome: `TINC_OK`,
  `TINC_ERR_SETUP_CANCELLED` or `TINC_ERR_SETUP_FAILED`, consumed once.
  Apps must handle "cancelled" without looping back into
  `tinc_openConfig()` forever. The TINCHND layout is defined in
  `src/tinc_config.c`; tinclib-config must match it.
- **Spike result (`tests/hw/handoff`, CEmu, OS 5.3.0 and 5.8.5 + arTIfiCE):**
  `os_RunPrgm` with a callback doesn't return and reliably brings control
  back. Chaining app → config → app by having TINCLIBC call
  `os_RunPrgm(return_to, ..., NULL)` **crashes the calculator (RAM reset)**,
  even with no tinclib code involved. Don't go back to the relaunch design.
  tinclib-config's AGENTS.md (which still says TINCLIBC relaunches
  `return_to`) needs updating to "exit when done".
- Seen in CEmu: a key pressed within about a second of `tinc_shutdown()`
  can be missed by `os_GetCSC()` (the hardware tests wait 3 s before
  [clear]). Not yet checked on real hardware.

## Constraints that shape everything here

- Target has very limited RAM. No `malloc`. Buffer sizes are compile-time
  constants the consuming program can tune.
- All wire-facing multi-byte values are little-endian, matching the eZ80 —
  but don't assume that means no work is needed. Build and parse payloads
  with protocol.h's byte-offset macros and `tinc_get_u16/u32`/`tinc_put_*`
  helpers, never packed structs, and use explicit types (`uint16_t`, not
  `int`): the eZ80's native `int` is 24 bits, which silently breaks
  assumptions carried over from 32-bit protocol code.
- No BLE/Bluetooth surface in this library in v1 — the underlying ESP8266
  firmware has none anyway.

## Testing

- **Host unit tests** (`make -C tests`, in CI under gcc + clang with
  ASan/UBSan): the library is built against stand-in CE headers in
  `tests/stubs/` and talks to a fake ESP (`stubs.c`) that uses
  tinclib-protocol's own parser/encoder. The golden-vector test checks the
  CE side of a whole conversation byte for byte against
  `test_vectors/vectors.h`. On Windows, run from PowerShell with MSYS2 gcc
  and `SANITIZE=` (Git Bash's DLLs break gcc silently). Add a test here for
  any new wire behavior.
- **CEmu hardware tests** (`make hw-test`, local only: they need a ROM):
  `canary`, `nodevice`, `handoff`. Each paints the screen green (pass) or
  red with the failing line, so there's one CRC per test. Local ROMs are in
  `C:\tools\CEmu\roms\`: `ti-84ce-v5.3.0.0037-base+clibs.rom` (Asm launch)
  and `ti-84ce-v5.8.5.0074-jailbreak+clibs.rom` (arTIfiCE launch). Both
  pass.
  - On OS 5.5+ the runner picks the program from the PRGM menu by its first
    letter, so test programs must sort before `TINCLIBC` (hence
    `TAHANDOF`). A plain `Asm(` launch on 5.5+ gives ERROR: INVALID. That
    was the cause of tinclib-config's CI failure with `ce-rom`.
  - The autotester doesn't show the `0xFB0000` debug console. Debug via
    the failure text on the verdict screen (the dump PNGs in
    `tests/hw/build/`).
- **`make size-check`** guards dead-code elimination (see above).
- **C++ bindings:** `tests/test_cpp.cpp` (built with `-fno-exceptions
  -fno-rtti`, like CEdev) runs the wrappers against the fake board, and
  `make size_cpp` builds `examples/size_min/main.cpp` with CEdev. The
  bindings cost 16 bytes over calling the C API from C++ (8,401 vs 8,385,
  measured at v0.4.0; `size_cpp` is 9,844 bytes at v0.5.0).

## C++ bindings

`src/tinclib.hpp` wraps the C API one to one and must stay that way: every
call is an inline forward, no exceptions (CEdev builds C++ with
`-fno-exceptions`), no `std::string` or other allocation. **No request
object**: it would bring back the request handle and suggest several
requests at once. The only class is `tinc::Session`, a scope guard for
`tinc_init`/`tinc_shutdown`. A new C function gets its `tinc::` forward in
the same change.

## Workflow

- Commits are authored by Gavin Smith only, `[dev] <summary>` style with a
  bulleted body. **No `Co-Authored-By: Claude` / "Generated with Claude
  Code" lines** in commits or PR bodies.
- Work on a branch per phase (`phase-N`), then a PR titled
  `[merge] vX.Y.Z from phase-N`. Tag `vX.Y.Z` (matching `TINC_VERSION`) to
  draft a release.
- When writing files from Python on this Windows machine, pass
  `encoding="utf-8"`: the default is cp1252, and a failed write truncates
  the file.
