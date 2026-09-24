/* Host test for the C++ bindings (tinclib.hpp) against the fake board. The
 * C tests cover the library; this checks the wrappers reach it. */

#include <stdio.h>
#include <string.h>

#include "tinclib.hpp"

extern "C" {
#include "fake_esp.h"
}

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void test_get()
{
    fake_reset();
    fake.req_polls = 1;
    fake.body = "hello from c++";
    fake.ctype = "text/plain";
    {
        tinc::Session net({ "MYAPP", 7, TINC_CF_ASCII });
        char body[32];
        uint16_t len = 0;
        tinc::State st;

        CHECK(net.err() == tinc::Ok);
        CHECK(tinc::isActive());
        CHECK(tinc::request(tinc::Method::Get, "https://example.com/", "A: b\r\n") == tinc::Ok);
        CHECK(strcmp(fake.headers, "A: b\r\n") == 0);
        CHECK(fake.req_flags == TINC_REQF_TRANSCODE && fake.req_timeout == 7);
        CHECK(tinc::poll() == tinc::State::Securing);
        while ((st = tinc::poll()) != tinc::State::Done && st != tinc::State::Error) {
            char chunk[4];
            int16_t n = tinc::read(chunk);   /* sized from the array */

            CHECK(n <= 4);
            memcpy(body + len, chunk, (size_t)n);
            len = (uint16_t)(len + n);
        }
        body[len] = '\0';
        CHECK(st == tinc::State::Done);
        CHECK(strcmp(body, "hello from c++") == 0);
        CHECK(tinc::httpStatus() == 200);
        CHECK(strcmp(tinc::contentType(), "text/plain") == 0);
        CHECK(tinc::request(tinc::Method::Post, "http://x/", nullptr, "a", 1) == tinc::Ok);
        while ((st = tinc::poll()) != tinc::State::Done && st != tinc::State::Error) {
            char chunk[16];
            char loc[8];

            if (st == tinc::State::Body)
                CHECK(tinc::header("Location", loc) == -1);
            tinc::read(chunk);
        }
        CHECK(st == tinc::State::Done);
        CHECK(fake.method == TINC_METHOD_POST && fake.upload_len == 1);
    }
    /* ~Session shut the link down. */
    CHECK(tinc::request(tinc::Method::Get, "http://x/") == TINC_ERR_NOT_INIT);
}

static void test_errors()
{
    fake_reset();
    fake.req_err = TINC_ERR_CERT;
    tinc::Session net;

    static const tinc_request_t req = { TINC_GET, "https://x/", nullptr, nullptr, 0 };

    CHECK(net.err() == tinc::Ok);
    CHECK(tinc::request(req) == tinc::Ok);
    CHECK(strcmp(fake.url, "https://x/") == 0);
    CHECK(tinc::poll() == tinc::State::Error);
    CHECK(tinc::error() == TINC_ERR_CERT);
    CHECK(tinc::errDetail() == 0);
    CHECK(strcmp(tinc::errString(tinc::error()), "Certificate rejected") == 0);
    CHECK(tinc::openConfig() == TINC_ERR_BAD_ARG);   /* no appName */
    tinc::abort();
    CHECK(tinc::poll() == tinc::State::Idle);
}

int main()
{
    test_get();
    test_errors();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("c++ bindings: all tests passed\n");
    return 0;
}
