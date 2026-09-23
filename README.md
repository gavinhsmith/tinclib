# tinclib

Wi-Fi and HTTP for TI-84 Plus CE programs. An ESP8266 board on the
calculator's USB port does the networking, running
[tinclib-firmware](https://github.com/gavinhsmith/tinclib-firmware-esp8266).
tinclib is the C library your program links against to talk to it, over the
[tinclib-protocol](https://github.com/gavinhsmith/tinclib-protocol) wire
protocol. Wi-Fi setup is left to the TINCLIBC config app
([tinclib-config](https://github.com/gavinhsmith/tinclib-config)).

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
- Protocol 0.3: plain `http://` GET only. HTTPS and POST come with later
  protocol versions. The board's firmware must speak the same protocol
  version (0.3): until 1.0, a mismatch fails `tinc_init()` with
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

See [CONTRIBUTING.md](CONTRIBUTING.md) to build and test tinclib itself.
