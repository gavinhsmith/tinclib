/*
 * size_full (main.c with TINC_SIZE_FULL) through the C++ bindings, built as
 * size_cpp. The bindings are all inline: with CEdev v15 this is 8,401 bytes,
 * 16 more than the same program calling the C API from C++ and 34 more than
 * size_full. Building the request from arguments instead of a static const
 * tinc_request_t costs about 65 more.
 */

#include <ti/getcsc.h>
#include <ti/screen.h>

#include "tinclib.hpp"

int main() {
    {
        tinc::Session net;
        bool online = net.err() == tinc::Ok && tinc::isActive();
        static const tinc_request_t req = { TINC_GET, "http://example.com/", NULL, NULL, 0 };
        char buf[16];

        os_ClrHome();
        os_PutStrFull(online ? "Online" : "Offline");
        if (!online)
            tinc::openConfig();
        if (tinc::request(req) == tinc::Ok)
            while (tinc::poll() == tinc::State::Body)
                tinc::read(buf);
        os_PutStrFull(tinc::errString(tinc::error()));
        os_PutStrFull(tinc::contentType());
        if (tinc::httpStatus() || tinc::errDetail())
            tinc::abort();
    }
    while (!os_GetCSC()) {
    }
    return 0;
}
