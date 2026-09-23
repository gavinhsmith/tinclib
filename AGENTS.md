# AGENTS.md — tinclib

## What this repo is

The CE-side C library (`tinclib.h` + `src/`) that TI-84 Plus CE programs
link against to get Wi-Fi/HTTP(S) connectivity through an ESP8266 board.
This is the library apps `#include`; it is **not** the config app
(`tinclib-config`) and **not** the firmware (`tinclib-firmware`) — keep
those concerns out of this repo.

Consumes `tinclib-protocol` as a pinned dependency. **Never fork or
hand-copy `protocol.h`/`crc16.c`.** If something needs a protocol change,
that goes in `tinclib-protocol` first.

## Toolchain

- **CE C/C++ Toolchain (CEdev)**, targeting the eZ80 in the TI-84 Plus CE.
- Serial transport is **`srldrvce`** (built on `usbdrvce`), used in host
  mode — the calculator is the USB host, the ESP board's bridge chip is
  the device.
- `usb_HandleEvents()` must be called regularly for the USB stack to work;
  library functions that touch the link (`tinc_poll()` etc.) should pump
  this internally so an app that only calls the high-level API still works
  without explicitly managing USB events itself.

## Distribution model: static, not LibLoad

LibLoad (a shared `.8xv` library apps link against at runtime) was
**explicitly rejected** — it requires the library to be written in raw
eZ80 assembly, which isn't realistic here, and LibLoad's documentation
situation makes it a bad bet regardless. **Do not propose LibLoad.** The
model is:
- Static linking: `tinclib.h` + a `src/` directory of `.c` files that a
  consuming program's build pulls in directly (copy, submodule, or a
  Makefile include — whichever the toolchain's project structure makes
  cleanest; confirm before assuming).
- Rely on the linker discarding unused functions/data (function/data
  sections + `-Oz`) so a program that only uses a few features doesn't pay
  for the whole library. **Verify this behavior with a real test program**
  before assuming it holds — don't just assert it works.
- Split source by feature (core/framing, Wi-Fi status, HTTP request
  handling) so optional pieces stay separable even without perfect dead-code
  elimination.
- State lives in a small number of static buffers/structs sized by
  compile-time defines (`TINC_RX_BUF_SIZE`, `TINC_MAX_SOCKETS`-equivalent,
  etc.) — **no `malloc`**. This is a RAM-constrained target; dynamic
  allocation was never on the table.

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

### Current shape (subject to refinement, but this is the agreed baseline)

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
tinc_state_t tinc_poll(void);   /* TINC_CONNECTING, TINC_SECURING, TINC_WAITING, TINC_BODY, TINC_DONE, TINC_ERROR */
int16_t      tinc_read(void *buf, uint16_t cap);        /* bytes read, 0 = nothing yet */
uint16_t     tinc_httpStatus(void);
const char  *tinc_contentType(void);
tinc_err_t   tinc_error(void);
const char  *tinc_errString(tinc_err_t e);
void         tinc_abort(void);

tinc_err_t   tinc_openConfig(const char *hint);   /* handoff to TINCLIBC; returns only on failure */
```

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
- No way to read a response header (e.g. `Location` after a redirect) —
  blocked on `HDR_GET` (`0x13`) being implemented in `tinclib-protocol`
  and `tinclib-firmware` first. If asked to add `tinc_header(name, out,
  cap)`, check that the wire message actually exists yet.

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
  but don't assume that means no work is needed; structs must still be
  explicitly packed and typed (`uint16_t`, not `int`) since the eZ80's
  native `int` is 24 bits, which silently breaks assumptions carried over
  from 32-bit protocol code.
- No BLE/Bluetooth surface in this library in v1 — the underlying ESP8266
  firmware has none anyway.
