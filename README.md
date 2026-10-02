# tinclib

[![CI](https://github.com/gavinhsmith/tinclib/actions/workflows/ci.yml/badge.svg)](https://github.com/gavinhsmith/tinclib/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/gavinhsmith/tinclib)](https://github.com/gavinhsmith/tinclib/releases)
[![License: Apache 2.0](https://img.shields.io/github/license/gavinhsmith/tinclib)](LICENSE)

**Wi-Fi, HTTP and HTTPS for TI-84 Plus CE programs.** Plug an ESP8266 board
into the calculator's USB port and your C or C++ program can fetch web pages,
call REST APIs and upload data.

- **HTTP and HTTPS**: GET, POST, PUT, PATCH, DELETE and HEAD, with
  certificate checking, request headers and response headers.
- **Small**: no `malloc`, static buffers sized at compile time, and the
  linker drops what you don't call (about 4 KB for a connection check, 10 KB
  for the whole API).
- **Simple**: one request at a time, driven by `tinc_poll()` from your main
  loop. No handles to free.
- **Wi-Fi setup handled for you**: `tinc_openConfig()` hands off to the
  TINCLIBC config app.
- **C and C++**: `tinclib.hpp` adds header-only bindings with no exceptions
  and no allocation.
- **Tested**: host unit tests (ASan/UBSan, gcc and clang) against a fake
  board, CEmu tests, and real hardware.

## How it fits together

tinclib is one part of four:

| Repo | What |
|---|---|
| **tinclib** (this one) | The C library your calculator program links against |
| [tinclib-firmware](https://github.com/gavinhsmith/tinclib-firmware) | Firmware for the ESP8266 board, which does the networking |
| [tinclib-config](https://github.com/gavinhsmith/tinclib-config) | TINCLIBC, the calculator app for Wi-Fi setup |
| [tinclib-protocol](https://github.com/gavinhsmith/tinclib-protocol) | The wire protocol between calculator and board |

You need a TI-84 Plus CE, an ESP8266 board whose USB bridge is CDC, FTDI or
PL2303 (the calculator's USB driver doesn't support CP210x or CH340), and the
[CE C/C++ Toolchain](https://github.com/CE-Programming/toolchain) to build.

## Example

```c
#include "tinclib.h"

static const tinc_config_t cfg = { "MYAPP", 0, TINC_CF_ASCII };
tinc_request_t req = { TINC_GET, "http://example.com/", NULL, NULL, 0 };
tinc_state_t st;
char buf[64];

if (tinc_init(&cfg) == TINC_OK && tinc_isActive(TINC_WIFI) && tinc_request(&req) == TINC_OK)
    while ((st = tinc_poll()) != TINC_DONE && st != TINC_ERROR) {
        int16_t n = tinc_read(buf, sizeof buf);
        /* use n bytes */
    }
tinc_shutdown();
```

`src/tinclib.h` is the whole API, with the details. In short:

- One request at a time. `tinc_request()` starts it, `tinc_poll()` drives it
  (call it from your main loop), `tinc_read()` copies out the body.
- No `malloc`. Buffers are static and sized at compile time
  (`TINC_RX_BUF_SIZE`).
- `tinc_openConfig()` sends the user to TINCLIBC. **Your program is unloaded
  and starts over from `main()` afterwards, so save your state first.**
  `tinc_init()` then tells you whether setup worked.
- Protocol 0.6: GET, POST, PUT, PATCH, DELETE and HEAD over `http://` or
  `https://`. A request body is streamed from your buffer, not copied, so
  keep it untouched until the state reaches `TINC_BODY`.
  `tinc_header()` reads a response header (e.g. `Location`). HTTPS
  certificates are always checked against CA roots built into the board's
  firmware (TLS 1.2 at most). The board's firmware must speak the same
  protocol version (0.6): until 1.0, a mismatch fails `tinc_init()` with
  `TINC_ERR_VERSION`.

See `examples/hello/` for a complete program.

## Using it

Download `tinclib-vX.Y.Z.zip` from the releases, unzip it into your CEdev
project's `lib/` folder, and add to your makefile:

```make
CFLAGS = -Wall -Wextra -Oz -Ilib/tinclib/src
EXTRA_C_SOURCES = $(wildcard lib/tinclib/src/*.c)
```

The library is compiled into your program, and the linker leaves out what you
don't call (`make size-check` verifies it: a program that only checks the
connection is about half the size of one using everything). The calculator
needs the [CE C libraries](https://github.com/CE-Programming/libraries/releases)
usbdrvce, srldrvce and fileioc.

### C++

`tinclib.h` works from C++ as is. `tinclib.hpp` adds thin, header-only
bindings: the same calls in namespace `tinc`, enum classes for states and
methods, and a `tinc::Session` that calls `tinc_init()` and, when it goes out
of scope, `tinc_shutdown()`. No exceptions, no allocation; errors are return
codes as in C. Add `-Ilib/tinclib/src` to `CXXFLAGS`.

```cpp
#include "tinclib.hpp"

tinc::Session net({ "MYAPP", 0, TINC_CF_ASCII });
if (net.err() == tinc::Ok && tinc::isActive() &&
    tinc::request(tinc::Method::Get, "https://example.com/") == tinc::Ok) {
    char buf[64];
    tinc::State st;
    while ((st = tinc::poll()) != tinc::State::Done && st != tinc::State::Error) {
        int16_t n = tinc::read(buf);   // size taken from the array
        // use n bytes of buf
    }
}
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) to build and test tinclib itself.

## License

[Apache 2.0](LICENSE)
