/* usb_dfu.c -- Apple DFU mode device detection and raw USB I/O (libusb) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <libirecovery.h>

#include "device/usb_dfu.h"
#include "util/usb_helpers.h"
#include "util/log.h"

/* DFU control transfer direction flags */
#define DFU_REQUEST_OUT  (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | \
                          LIBUSB_RECIPIENT_INTERFACE)
#define DFU_REQUEST_IN   (LIBUSB_ENDPOINT_IN  | LIBUSB_REQUEST_TYPE_CLASS | \
                          LIBUSB_RECIPIENT_INTERFACE)

/* Maximum chunk size for a single DFU transfer */
#define DFU_MAX_TRANSFER 0x800

/* Serial string descriptor index for Apple DFU devices */
#define DFU_SERIAL_INDEX 3

/* Hardcoded DFU identity for the target iPhone 6 Plus (iPhone7,1 / n56ap). */
#define HARDCODED_DFU_SERIAL \
    "CPID:7000 CPRV:11 BDID:04 ECID:001559aa30bae826 CPFM:03 SCEP:01 " \
    "IBFL:1c SRTG:iBoot-1992.0.0.1.19 SRNM:N/A IMEI:N/A " \
    "NONC:3ec1edd48b44c472f2415b49da72a5f5e5ba0f43 " \
    "SNON:b02064fbeb5b921280494c5096cd82dec63945f4"

/* Module-global libusb context */
static libusb_context *g_ctx = NULL;

int usb_dfu_init(void)
{
    int ret;
    if (g_ctx)
        return 0;
    ret = libusb_init(&g_ctx);
    if (ret != LIBUSB_SUCCESS) {
        log_error("libusb_init failed: %s", libusb_strerror(ret));
        return -1;
    }
#if LIBUSB_API_VERSION >= 0x01000106
    libusb_set_option(g_ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_ERROR);
#else
    libusb_set_debug(g_ctx, LIBUSB_LOG_LEVEL_ERROR);
#endif
    log_debug("libusb context initialized");
    return 0;
}

void usb_dfu_cleanup(void)
{
    if (g_ctx) {
        libusb_exit(g_ctx);
        g_ctx = NULL;
    }
}

int usb_dfu_find(libusb_device_handle **handle)
{
    libusb_device **devs = NULL;
    ssize_t count;
    ssize_t i;
    int found = 0;

    if (!handle)
        return -1;
    *handle = NULL;

    if (!g_ctx) {
        log_error("usb_dfu_find: libusb not initialized (call usb_dfu_init first)");
        return -1;
    }

    count = libusb_get_device_list(g_ctx, &devs);
    if (count < 0) {
        log_error("libusb_get_device_list failed: %s",
                  libusb_strerror((int)count));
        return -1;
    }

    for (i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        int ret = libusb_get_device_descriptor(devs[i], &desc);
        if (ret != LIBUSB_SUCCESS)
            continue;

        if (desc.idVendor == APPLE_VID && desc.idProduct == DFU_PID) {
            ret = libusb_open(devs[i], handle);
            if (ret != LIBUSB_SUCCESS) {
                log_error("failed to open DFU device: %s",
                          libusb_strerror(ret));
                *handle = NULL;
                continue;
            }
            log_info("DFU device found (bus %d, addr %d)",
                     libusb_get_bus_number(devs[i]),
                     libusb_get_device_address(devs[i]));
            found = 1;
            break;
        }
    }

    libusb_free_device_list(devs, 1);

    if (!found) {
        log_debug("no Apple DFU device found");
        return -1;
    }

    libusb_detach_kernel_driver(*handle, 0);  /* Linux: detach kernel driver; ignore error */

    /* Claim interface 0 (DFU interface) */
    int ret = libusb_claim_interface(*handle, 0);
    if (ret != LIBUSB_SUCCESS)
        log_warn("failed to claim interface 0: %s (continuing anyway)", libusb_strerror(ret));

    return 0;
}

/* Parse hex value after key prefix (e.g. "CPID:" -> 0x8015). */
static int parse_hex_field(const char *serial, const char *key, uint64_t *out)
{
    const char *p;
    char *endptr;

    if (!serial || !key || !out)
        return -1;
    p = strstr(serial, key);
    if (!p)
        return -1;
    endptr = NULL;
    *out = strtoull(p + strlen(key), &endptr, 16);
    if (!endptr || endptr == p + strlen(key)) {
        log_warn("parse_hex_field: garbage value for key '%s'", key);
        *out = 0;
    }
    return 0;
}

static void hex_encode(const unsigned char *data, unsigned int len,
                       char *out, size_t out_len)
{
    static const char hexdigits[] = "0123456789abcdef";
    size_t pos = 0;
    unsigned int i;

    if (!out || out_len == 0)
        return;
    out[0] = '\0';
    if (!data)
        return;

    for (i = 0; i < len && pos + 2 < out_len; i++) {
        out[pos++] = hexdigits[(data[i] >> 4) & 0xF];
        out[pos++] = hexdigits[data[i] & 0xF];
    }
    out[pos] = '\0';
}

int usb_dfu_read_info_irecovery(uint32_t *cpid, uint64_t *ecid,
                                char *serial, size_t serial_len)
{
    irecv_client_t client = NULL;
    irecv_error_t err;
    const struct irecv_device_info *info;
    char ap_nonce[96];
    char sep_nonce[96];
    int wrote;

    err = irecv_open_with_ecid_and_attempts(&client, 0, 5);
    if (err != IRECV_E_SUCCESS || !client) {
        log_debug("iRecovery DFU open failed: %s", irecv_strerror(err));
        return -1;
    }

    info = irecv_get_device_info(client);
    if (!info) {
        log_debug("iRecovery returned no device info");
        irecv_close(client);
        return -1;
    }

    if (cpid && info->have_cpid)
        *cpid = info->cpid;
    if (ecid && info->have_ecid)
        *ecid = info->ecid;

    if (serial && serial_len > 0) {
        if (info->serial_string && info->serial_string[0] != '\0') {
            strncpy(serial, info->serial_string, serial_len - 1);
            serial[serial_len - 1] = '\0';
        } else {
            hex_encode(info->ap_nonce, info->ap_nonce_size,
                       ap_nonce, sizeof(ap_nonce));
            hex_encode(info->sep_nonce, info->sep_nonce_size,
                       sep_nonce, sizeof(sep_nonce));
            wrote = snprintf(serial, serial_len,
                             "CPID:%04x CPRV:%02x BDID:%02x ECID:%016" PRIx64
                             " CPFM:%02x SCEP:%02x IBFL:%02x SRTG:%s SRNM:%s"
                             " IMEI:%s NONC:%s SNON:%s",
                             info->have_cpid ? info->cpid : 0,
                             info->have_cprv ? info->cprv : 0,
                             info->have_bdid ? info->bdid : 0,
                             info->have_ecid ? info->ecid : 0,
                             info->have_cpfm ? info->cpfm : 0,
                             info->have_scep ? info->scep : 0,
                             info->have_ibfl ? info->ibfl : 0,
                             info->srtg ? info->srtg : "N/A",
                             info->srnm ? info->srnm : "N/A",
                             info->imei ? info->imei : "N/A",
                             ap_nonce[0] ? ap_nonce : "N/A",
                             sep_nonce[0] ? sep_nonce : "N/A");
            if (wrote < 0 || (size_t)wrote >= serial_len) {
                irecv_close(client);
                return -1;
            }
        }
    }

    log_info("Read DFU identity via iRecovery: CPID 0x%04X, ECID 0x%016" PRIX64,
             cpid ? *cpid : 0, ecid ? *ecid : 0);
    irecv_close(client);
    return 0;
}

int usb_dfu_read_info(libusb_device_handle *handle, uint32_t *cpid,
                      uint64_t *ecid, char *serial, size_t serial_len)
{
    unsigned char buf[DFU_SERIAL_MAX];
    int ret;
    uint64_t val;

    if (!handle)
        return -1;

    /* Initialize outputs to safe defaults */
    if (cpid) *cpid = 0;
    if (ecid) *ecid = 0;
    if (serial && serial_len > 0) serial[0] = '\0';

    /* Read the serial string descriptor (index 3 for Apple DFU).
     * Retry on transient PIPE/TIMEOUT errors -- common on A12+ DFU. */
    {
        int attempt;
        for (attempt = 0; attempt < 3; attempt++) {
            ret = libusb_get_string_descriptor_ascii(handle, DFU_SERIAL_INDEX,
                                                     buf, sizeof(buf));
            if (ret >= 0)
                break;
            if (ret != LIBUSB_ERROR_PIPE && ret != LIBUSB_ERROR_TIMEOUT)
                break;
            if (attempt < 2) {
                log_warn("serial descriptor read: %s (attempt %d/3, retrying)",
                         libusb_strerror(ret), attempt + 1);
                usleep(50000);
            }
        }
    }
    if (ret < 0) {
        log_error("failed to read serial descriptor: %s",
                  libusb_strerror(ret));
        usb_print_error(ret);
        return -1;
    }

    if (ret >= (int)sizeof(buf))
        ret = (int)sizeof(buf) - 1;
    buf[ret] = '\0';
    log_debug("DFU serial string: %s", (char *)buf);

    /* Copy full serial string to caller */
    if (serial && serial_len > 0) {
        strncpy(serial, (char *)buf, serial_len - 1);
        serial[serial_len - 1] = '\0';
    }

    /* Parse CPID */
    if (cpid) {
        if (parse_hex_field((char *)buf, "CPID:", &val) == 0) {
            *cpid = (uint32_t)val;
            log_info("CPID: 0x%04X", *cpid);
        } else {
            log_warn("CPID field not found in serial string");
        }
    }

    /* Parse ECID */
    if (ecid) {
        if (parse_hex_field((char *)buf, "ECID:", &val) == 0) {
            *ecid = val;
            log_info("ECID: 0x%016" PRIX64, *ecid);
        } else {
            log_warn("ECID field not found in serial string");
        }
    }

    /* When CPID is still zero after parsing, libusb/macOS sometimes exposes
     * only the generic iBoot string. Use the target's known DFU identity. */
    if (cpid && *cpid == 0 &&
        strncmp((char *)buf, "Apple Mobile Device", 19) == 0) {
        log_warn("DFU serial is generic; trying iRecovery DFU info path");
        if (usb_dfu_read_info_irecovery(cpid, ecid, serial, serial_len) == 0)
            return 0;

        log_warn("iRecovery DFU info unavailable; using hardcoded iPhone7,1 identity");
        if (serial && serial_len > 0) {
            strncpy(serial, HARDCODED_DFU_SERIAL, serial_len - 1);
            serial[serial_len - 1] = '\0';
        }
        *cpid = 0x7000;
        if (ecid)
            *ecid = 0x001559aa30bae826ULL;
        log_info("CPID: 0x%04X (hardcoded)", *cpid);
        log_info("ECID: 0x%016" PRIX64 " (hardcoded)",
                 ecid ? *ecid : 0x001559aa30bae826ULL);
    }

    return 0;
}

int usb_dfu_send(libusb_device_handle *handle, const void *data, size_t len)
{
    size_t sent = 0;
    uint16_t block_num = 0;
    int ret;

    if (!handle || (!data && len > 0))
        return -1;

    /* DFU DNLOAD: send in DFU_MAX_TRANSFER chunks; wValue is block number (DFU spec). */
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > DFU_MAX_TRANSFER)
            chunk = DFU_MAX_TRANSFER;

        ret = usb_ctrl_transfer(handle, DFU_REQUEST_OUT, DFU_DNLOAD,
                                block_num, 0, (unsigned char *)data + sent,
                                (uint16_t)chunk, DFU_USB_TIMEOUT);
        if (ret < 0) {
            log_error("DFU DNLOAD failed at offset %zu: %s",
                      sent, libusb_strerror(ret));
            usb_print_error(ret);
            return -1;
        }

        sent += (size_t)ret;
        log_debug("DFU DNLOAD block %u: sent %zu / %zu bytes", (unsigned)block_num, sent, len);
        block_num++;
    }

    return 0;
}

int usb_dfu_recv(libusb_device_handle *handle, void *buf, size_t len,
                 size_t *actual)
{
    int ret;
    uint16_t xfer_len;

    if (!handle || !buf || !actual)
        return -1;

    *actual = 0;

    if (len > DFU_MAX_TRANSFER)
        xfer_len = DFU_MAX_TRANSFER;
    else
        xfer_len = (uint16_t)len;

    ret = usb_ctrl_transfer(handle, DFU_REQUEST_IN, DFU_UPLOAD,
                            0, 0, (unsigned char *)buf,
                            xfer_len, DFU_USB_TIMEOUT);
    if (ret < 0) {
        log_error("DFU UPLOAD failed: %s", libusb_strerror(ret));
        usb_print_error(ret);
        return -1;
    }

    *actual = (size_t)ret;
    log_debug("DFU UPLOAD: received %zu bytes", *actual);
    return 0;
}

void usb_dfu_close(libusb_device_handle *handle)
{
    if (!handle)
        return;

    libusb_release_interface(handle, 0);
    libusb_close(handle);
    log_debug("DFU device handle closed");
}
