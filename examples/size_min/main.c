/*
 * Smallest tinclib program: is the board there and online? Built twice for
 * `make size-check`: as is (size_min), and with TINC_SIZE_FULL (size_full)
 * also using the request API and the handoff. The difference shows whether
 * the linker leaves out the parts of tinclib a program doesn't call.
 */

#include <ti/getcsc.h>
#include <ti/screen.h>

#include "tinclib.h"

int main(void) {
    bool online;

    os_ClrHome();
    online = tinc_init(NULL) == TINC_OK && tinc_isActive(TINC_WIFI);
    os_PutStrFull(online ? "Online" : "Offline");
#ifdef TINC_SIZE_FULL
    {
        static const tinc_request_t req = { TINC_POST, "http://example.com/", NULL, "a", 1 };
        char buf[16];

        if (!online)
            tinc_openConfig(NULL);
        if (tinc_request(&req) == TINC_OK)
            while (tinc_poll() == TINC_BODY)
                tinc_read(buf, sizeof buf);
        os_PutStrFull(tinc_errString(tinc_error()));
        os_PutStrFull(tinc_contentType());
        if (tinc_httpStatus() || tinc_errDetail() || tinc_header("Location", buf, sizeof buf) > 0)
            tinc_abort();
    }
#endif
    tinc_shutdown();
    while (!os_GetCSC()) {
    }
    return 0;
}
