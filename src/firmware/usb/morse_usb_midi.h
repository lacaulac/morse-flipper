#pragma once

#include <stddef.h>
#include <stdint.h>

#include <furi_hal_usb.h>

extern FuriHalUsbInterface morse_usb_midi_interface;

bool morse_usb_midi_is_connected(void);
size_t morse_usb_midi_rx(uint8_t* buffer, size_t size);
size_t morse_usb_midi_tx(const uint8_t* buffer, size_t size);
