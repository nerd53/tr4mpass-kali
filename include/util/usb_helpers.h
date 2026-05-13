#ifndef USB_HELPERS_H
#define USB_HELPERS_H

#include <stdint.h>
#include <libusb.h>

/*
 * Perform a USB control transfer with a data stage.
 * Returns number of bytes transferred on success, negative libusb error code
 * on failure.
 */
int usb_ctrl_transfer(libusb_device_handle *dev,
                      uint8_t bmRequestType,
                      uint8_t bRequest,
                      uint16_t wValue,
                      uint16_t wIndex,
                      unsigned char *data,
                      uint16_t wLength,
                      unsigned int timeout);

/*
 * Perform one raw USB control transfer with no retry policy.
 * Use this when PIPE/STALL/TIMEOUT semantics matter to the caller.
 */
int usb_ctrl_transfer_raw(libusb_device_handle *dev,
                          uint8_t bmRequestType,
                          uint8_t bRequest,
                          uint16_t wValue,
                          uint16_t wIndex,
                          unsigned char *data,
                          uint16_t wLength,
                          unsigned int timeout);

/*
 * Perform a USB control transfer with no data stage (wLength=0).
 * Returns 0 on success, negative libusb error code on failure.
 */
int usb_ctrl_transfer_no_data(libusb_device_handle *dev,
                              uint8_t bmRequestType,
                              uint8_t bRequest,
                              uint16_t wValue,
                              uint16_t wIndex,
                              unsigned int timeout);

/*
 * Perform one raw no-data USB control transfer with no retry policy.
 */
int usb_ctrl_transfer_no_data_raw(libusb_device_handle *dev,
                                  uint8_t bmRequestType,
                                  uint8_t bRequest,
                                  uint16_t wValue,
                                  uint16_t wIndex,
                                  unsigned int timeout);

/*
 * Returns 1 when a libusb error means the current handle is no longer usable.
 */
int usb_is_disconnect_error(int libusb_error);

/*
 * Print a human-readable description of a libusb error code to stderr.
 */
void usb_print_error(int libusb_error);

#endif /* USB_HELPERS_H */
