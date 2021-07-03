/* Copyright 2021 QMK
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "split_util.h"
#include "matrix.h"
#include "keyboard.h"
#include "config.h"
#include "timer.h"
#include "transport.h"
#include "quantum.h"
#include "wait.h"
#include "usb_util.h"

#ifdef EE_HANDS
#    include "eeconfig.h"
#endif

#if defined(RGBLIGHT_ENABLE) && defined(RGBLED_SPLIT)
#    include "rgblight.h"
#endif

#ifndef SPLIT_USB_TIMEOUT
#    define SPLIT_USB_TIMEOUT 2000
#endif

#ifndef SPLIT_USB_TIMEOUT_POLL
#    define SPLIT_USB_TIMEOUT_POLL 10
#endif

volatile bool isLeftHand = true;

#if defined(SPLIT_USB_DETECT)
static bool usbIsActive(void) {
    for (uint8_t i = 0; i < (SPLIT_USB_TIMEOUT / SPLIT_USB_TIMEOUT_POLL); i++) {
        // This will return true if a USB connection has been established
        if (usb_connected_state()) {
            return true;
        }
        wait_ms(SPLIT_USB_TIMEOUT_POLL);
    }
    return false;
}
#else
static inline bool usbIsActive(void) { return usb_vbus_state(); }
#endif

#ifdef SPLIT_HAND_MATRIX_GRID
void matrix_io_delay(void);

static uint8_t peek_matrix_intersection(pin_t out_pin, pin_t in_pin) {
    setPinInputHigh(in_pin);
    setPinOutput(out_pin);
    writePinLow(out_pin);
    // It's almost unnecessary, but wait until it's down to low, just in case.
    wait_us(1);
    uint8_t pin_state = readPin(in_pin);
    // Set out_pin to a setting that is less susceptible to noise.
    setPinInputHigh(out_pin);
    matrix_io_delay();  // Wait for the pull-up to go HIGH.
    return pin_state;
}
#endif

__attribute__((weak)) bool is_keyboard_left(void) {
#if defined(SPLIT_HAND_PIN)
    // Test pin SPLIT_HAND_PIN for High/Low, if low it's right hand
    setPinInput(SPLIT_HAND_PIN);
    return readPin(SPLIT_HAND_PIN);
#elif defined(SPLIT_HAND_MATRIX_GRID)
#    ifdef SPLIT_HAND_MATRIX_GRID_LOW_IS_RIGHT
    return peek_matrix_intersection(SPLIT_HAND_MATRIX_GRID);
#    else
    return !peek_matrix_intersection(SPLIT_HAND_MATRIX_GRID);
#    endif
#elif defined(EE_HANDS)
    return eeconfig_read_handedness();
#elif defined(MASTER_RIGHT)
    return !is_keyboard_master();
#endif

    return is_keyboard_master();
}

__attribute__((weak)) bool is_keyboard_master(void) {
    static enum { UNKNOWN, MASTER, SLAVE } usbstate = UNKNOWN;

    // only check once, as this is called often
    if (usbstate == UNKNOWN) {
        usbstate = usbIsActive() ? MASTER : SLAVE;

        // Avoid NO_USB_STARTUP_CHECK - Disable USB as the previous checks seem to enable it somehow
        if (usbstate == SLAVE) {
            usb_disable();
        }
    }

    return (usbstate == MASTER);
}

// this code runs before the keyboard is fully initialized
void split_pre_init(void) {
    isLeftHand = is_keyboard_left();

#if defined(RGBLIGHT_ENABLE) && defined(RGBLED_SPLIT)
    uint8_t num_rgb_leds_split[2] = RGBLED_SPLIT;
    if (isLeftHand) {
        rgblight_set_clipping_range(0, num_rgb_leds_split[0]);
    } else {
        rgblight_set_clipping_range(num_rgb_leds_split[0], num_rgb_leds_split[1]);
    }
#endif

    if (is_keyboard_master()) {
#if defined(USE_I2C) && defined(SSD1306OLED)
        matrix_master_OLED_init();
#endif
        transport_master_init();
    }
}

// this code runs after the keyboard is fully initialized
//   - avoids race condition during matrix_init_quantum where slave can start
//     receiving before the init process has completed
void split_post_init(void) {
    if (!is_keyboard_master()) {
        transport_slave_init();
    }
}
