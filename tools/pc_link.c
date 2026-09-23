/*
 * Runs tinclib on a Windows PC against a real ESP board on a COM port, in
 * place of the calculator. The library sources are the ones the calculator
 * builds; only srldrvce/usbdrvce (here: Win32 serial), fileioc and
 * os_RunPrgm are swapped out, using the stand-in headers from tests/stubs.
 *
 *     make pc-link
 *     bin/pc_link.exe COM5                       # handshake + Wi-Fi status
 *     bin/pc_link.exe COM5 http://example.com/   # and a GET
 *
 * The board needs Wi-Fi credentials already (tinclib-firmware's
 * tools/serial/wifi.example.txt sets them).
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <fileioc.h>
#include <srldrvce.h>
#include <ti/vars.h>
#include <usbdrvce.h>

#include "tinc_internal.h"

/* After the CE headers: windows.h defines `interface` as a macro. */
#include <windows.h>

static HANDLE port = INVALID_HANDLE_VALUE;
static const char *port_name;

/* ---- usbdrvce / srldrvce over a COM port ------------------------------ */

static usb_event_callback_t handler;
static bool announced;
static struct usb_device { int unused; } the_device;

usb_error_t usb_Init(usb_event_callback_t h, usb_callback_data_t *data,
                     const usb_standard_descriptors_t *desc, unsigned flags)
{
    char path[64];
    DCB dcb = { .DCBlength = sizeof dcb };
    COMMTIMEOUTS to = { .ReadIntervalTimeout = MAXDWORD }; /* reads return at once */

    (void)data;
    (void)desc;
    (void)flags;
    snprintf(path, sizeof path, "\\\\.\\%s", port_name);
    port = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (port == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "can't open %s (Windows error %lu)\n", port_name, GetLastError());
        return USB_ERROR_FAILED;
    }
    GetCommState(port, &dcb);
    dcb.BaudRate = TINC_BAUD_DEFAULT;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    /* ESP boards wire DTR/RTS to reset and GPIO0: keep them released so
     * opening the port doesn't reset the board or enter the bootloader. */
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = dcb.fInX = FALSE;
    SetCommState(port, &dcb);
    SetCommTimeouts(port, &to);
    PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR);
    handler = h;
    announced = false;
    return USB_SUCCESS;
}

void usb_Cleanup(void)
{
    if (port != INVALID_HANDLE_VALUE)
        CloseHandle(port);
    port = INVALID_HANDLE_VALUE;
    handler = NULL;
}

usb_error_t usb_HandleEvents(void)
{
    if (handler && port != INVALID_HANDLE_VALUE && !announced) {
        announced = true;
        handler(USB_DEVICE_CONNECTED_EVENT, &the_device, NULL);
        handler(USB_DEVICE_ENABLED_EVENT, &the_device, NULL);
    }
    return USB_SUCCESS;
}

usb_device_t usb_FindDevice(usb_device_t root, usb_device_t from, unsigned flags)
{
    (void)root;
    (void)from;
    (void)flags;
    return NULL;
}

usb_error_t usb_ResetDevice(usb_device_t device)
{
    (void)device;
    return USB_SUCCESS;
}

usb_role_t usb_GetRole(void)
{
    return 0;
}

srl_error_t srl_Open(srl_device_t *srl, usb_device_t dev, void *buffer, size_t size,
                     uint8_t iface, uint24_t rate)
{
    (void)buffer;
    (void)size;
    (void)iface;
    (void)rate;
    srl->dev = dev;
    return SRL_SUCCESS;
}

void srl_Close(srl_device_t *srl)
{
    srl->dev = NULL;
}

int srl_Read(srl_device_t *srl, void *data, size_t length)
{
    DWORD n = 0;

    (void)srl;
    return ReadFile(port, data, (DWORD)length, &n, NULL) ? (int)n : -1;
}

int srl_Write(srl_device_t *srl, const void *data, size_t length)
{
    DWORD n = 0;

    (void)srl;
    return WriteFile(port, data, (DWORD)length, &n, NULL) ? (int)n : -1;
}

const usb_standard_descriptors_t *srl_GetCDCStandardDescriptors(void)
{
    return NULL;
}

usb_error_t srl_UsbEventCallback(usb_event_t event, void *event_data,
                                 usb_callback_data_t *callback_data)
{
    (void)event;
    (void)event_data;
    (void)callback_data;
    return USB_SUCCESS;
}

/* ---- No appvars, no TINCLIBC on a PC ---------------------------------- */

uint8_t ti_Open(const char *name, const char *mode)
{
    (void)name;
    (void)mode;
    return 0;
}

int ti_Close(uint8_t h)
{
    (void)h;
    return 0;
}

size_t ti_Write(const void *d, size_t s, size_t c, uint8_t h)
{
    (void)d;
    (void)s;
    (void)c;
    (void)h;
    return 0;
}

size_t ti_Read(void *d, size_t s, size_t c, uint8_t h)
{
    (void)d;
    (void)s;
    (void)c;
    (void)h;
    return 0;
}

int ti_Seek(int o, unsigned int w, uint8_t h)
{
    (void)o;
    (void)w;
    (void)h;
    return EOF;
}

int ti_Delete(const char *name)
{
    (void)name;
    return 0;
}

int os_RunPrgm(const char *prgm, void *data, size_t size, os_runprgm_callback_t cb)
{
    (void)prgm;
    (void)data;
    (void)size;
    (void)cb;
    return -1;
}

/* ---- The test --------------------------------------------------------- */

static const char *state_name(tinc_state_t st)
{
    static const char *names[] = { "IDLE", "CONNECTING", "SECURING", "WAITING",
                                   "BODY", "DONE", "ERROR" };
    return st <= TINC_ERROR ? names[st] : "?";
}

int main(int argc, char **argv)
{
    static const tinc_config_t cfg = { "PCLINK", 0, TINC_CF_ASCII };
    tinc_request_t req = { TINC_GET, NULL, NULL, NULL, 0 };
    tinc_state_t st, last = TINC_IDLE;
    clock_t start;
    tinc_err_t err;
    unsigned long total = 0;
    char buf[256];

    if (argc < 2) {
        fprintf(stderr, "usage: %s COMn [url]\n", argv[0]);
        return 2;
    }
    port_name = argv[1];

    start = clock();
    err = tinc_init(&cfg);
    printf("tinc_init: %s (%ld ms)\n", tinc_errString(err),
           (long)((clock() - start) * 1000 / CLOCKS_PER_SEC));
    if (err != TINC_OK)
        return 1;

    start = clock();
    printf("tinc_isActive(TINC_WIFI): %s (%ld ms)\n", tinc_isActive(TINC_WIFI) ? "yes" : "no",
           (long)((clock() - start) * 1000 / CLOCKS_PER_SEC));

    /* The raw STATUS, which the API doesn't expose. */
    if (tinc_xfer(TINC_T_STATUS, NULL, 0, 0) == TINC_OK && tinc_g.parser.len >= TINC_STATUS_RESP_LEN) {
        static const char *wifi[] = { "NO_CREDS", "CONNECTING", "CONNECTED", "FAILED" };
        const uint8_t *r = tinc_g.parser.payload;
        uint8_t w = r[TINC_STATUS_WIFI_STATE];

        printf("STATUS: wifi %s%s, slot %d, rssi %d, ip %u.%u.%u.%u, free heap %lu\n",
               w < 4 ? wifi[w] : "?",
               r[TINC_STATUS_FLAGS] & TINC_STATUSF_WIFI_LOCKED ? " (locked)" : "",
               r[TINC_STATUS_SLOT] == TINC_SLOT_NONE ? -1 : r[TINC_STATUS_SLOT],
               (int8_t)r[TINC_STATUS_RSSI], r[TINC_STATUS_IP], r[TINC_STATUS_IP + 1],
               r[TINC_STATUS_IP + 2], r[TINC_STATUS_IP + 3],
               (unsigned long)tinc_get_u32(r + TINC_STATUS_FREE_HEAP));
    }

    if (argc > 2) {
        req.url = argv[2];
        start = clock();
        err = tinc_request(&req);
        printf("tinc_request(%s): %s\n", req.url, tinc_errString(err));
        if (err == TINC_OK) {
            while ((st = tinc_poll()) != TINC_DONE && st != TINC_ERROR) {
                int16_t n;

                if (st != last) {
                    printf("  state %s\n", state_name(st));
                    if (st == TINC_BODY)
                        printf("  HTTP %u, %s\n", tinc_httpStatus(), tinc_contentType());
                    last = st;
                }
                while ((n = tinc_read(buf, sizeof buf)) > 0) {
                    fwrite(buf, 1, (size_t)n, stdout);
                    total += (unsigned long)n;
                }
            }
            printf("\n  state %s: %s, %lu body bytes in %ld ms\n", state_name(st),
                   tinc_errString(tinc_error()), total,
                   (long)((clock() - start) * 1000 / CLOCKS_PER_SEC));
        }
    }

    tinc_shutdown();
    return err == TINC_OK ? 0 : 1;
}
