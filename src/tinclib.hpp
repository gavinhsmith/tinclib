/**
 * @file tinclib.hpp
 * @brief C++ bindings for tinclib: the same API as tinclib.h, in namespace
 *        tinc, with enum classes and a Session that shuts the link down.
 *
 * Header only and all inline, so it costs nothing over the C API. No
 * exceptions (CEdev builds C++ with -fno-exceptions) and no allocation:
 * errors are tinc::Err return codes, as in C. Still one request at a time,
 * owned by the library; see tinclib.h for the details of every call.
 *
 * @code
 * tinc::Session net({ "MYAPP", 0, TINC_CF_ASCII });
 * if (net.err() == tinc::Ok && tinc::isActive() &&
 *     tinc::request(tinc::Method::Get, "https://example.com/") == tinc::Ok) {
 *     char buf[64];
 *     tinc::State st;
 *     while ((st = tinc::poll()) != tinc::State::Done && st != tinc::State::Error) {
 *         int16_t n = tinc::read(buf);
 *         // use n bytes of buf
 *     }
 * }
 * // ~Session calls tinc_shutdown()
 * @endcode
 */
#ifndef TINCLIB_HPP
#define TINCLIB_HPP

#include <stddef.h>

#include "tinclib.h"

namespace tinc {

/** tinc_err_t: TINC_OK, TINC_ERR_* (protocol.h and tinclib.h). */
using Err = tinc_err_t;
using Config = tinc_config_t;

constexpr Err Ok = TINC_OK;

enum class State : uint8_t {
    Idle = TINC_IDLE,
    Connecting = TINC_CONNECTING,
    Securing = TINC_SECURING,
    Waiting = TINC_WAITING,
    Body = TINC_BODY,
    Done = TINC_DONE,
    Error = TINC_ERROR
};

enum class Method : uint8_t {
    Get = TINC_GET,
    Post = TINC_POST
};

enum class Mode : uint8_t {
    Wifi = TINC_WIFI
};

inline Err init(const Config *cfg = nullptr) { return tinc_init(cfg); }
inline bool isActive(Mode mode = Mode::Wifi) { return tinc_isActive(static_cast<tinc_mode_t>(mode)); }
inline void shutdown() { tinc_shutdown(); }

/**
 * Starts a request; see tinc_request(). body is NOT COPIED: it must stay
 * valid until poll() reaches State::Body (or the request ends).
 */
inline Err request(Method method, const char *url, const char *headers = nullptr,
                   const void *body = nullptr, uint16_t bodyLen = 0)
{
    const tinc_request_t req = { static_cast<tinc_method_t>(method), url, headers, body, bodyLen };
    return tinc_request(&req);
}

/** The same with a tinc_request_t; a static const one is the smallest code. */
inline Err request(const tinc_request_t &req) { return tinc_request(&req); }

inline State poll() { return static_cast<State>(tinc_poll()); }
inline int16_t read(void *buf, uint16_t cap) { return tinc_read(buf, cap); }

/** read() into an array, sized for you. */
template <typename T, size_t N>
inline int16_t read(T (&buf)[N])
{
    static_assert(sizeof buf <= 0xFFFF, "tinc::read: buffer over 65535 bytes");
    return tinc_read(buf, static_cast<uint16_t>(sizeof buf));
}

inline uint16_t httpStatus() { return tinc_httpStatus(); }
inline const char *contentType() { return tinc_contentType(); }
inline Err error() { return tinc_error(); }
inline uint8_t errDetail() { return tinc_errDetail(); }
inline const char *errString(Err e) { return tinc_errString(e); }
inline void abort() { tinc_abort(); }

/** Does not return when it works; see tinc_openConfig(). */
inline Err openConfig(const char *hint = nullptr) { return tinc_openConfig(hint); }

/**
 * tinc_init() for as long as it is in scope, tinc_shutdown() at the end.
 * err() is what tinc_init() returned, including TINC_ERR_SETUP_CANCELLED /
 * TINC_ERR_SETUP_FAILED after a trip to TINCLIBC (the link is up then).
 * One at a time, like the library.
 */
class Session {
public:
    explicit Session(const Config *cfg = nullptr) : err_(tinc_init(cfg)) {}
    explicit Session(const Config &cfg) : err_(tinc_init(&cfg)) {}
    ~Session() { tinc_shutdown(); }

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    Err err() const { return err_; }

private:
    Err err_;
};

} // namespace tinc

#endif /* TINCLIB_HPP */
