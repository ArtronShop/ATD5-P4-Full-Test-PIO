#pragma once
// ---------------------------------------------------------------------------
// Board / test configuration for the hardware test GUI.
// Edit this file to match the pins that are actually wired on your board.
// ---------------------------------------------------------------------------

// Grove I2C port (module "I2C"). Defaults are the ESP32-P4 Arduino default SDA/SCL.
// NOTE: verify against the ATD5-P4 schematic.
#define GUI_GROVE_SDA          7
#define GUI_GROVE_SCL          8
#define GUI_GROVE_FREQ_HZ      100000

// GPIOs shown in the IO test page (8 tiles, 4 x 2).
// Do not list pins used by LCD / touch / SD / I2S (see lib/ATD5-P4/src/Common.h).
#define GUI_IO_PIN_COUNT       8
#define GUI_IO_PINS            { 1, 2, 3, 4, 5, 9, 10, 11 }

// On-board LED (active LOW). It is put back to "off" when leaving the IO page.
#define GUI_LED_PIN            12

// Clock shown in the status bar (set from NTP after WiFi connects).
#define GUI_TZ_OFFSET_SEC      (7 * 3600)   // UTC+7
#define GUI_NTP_SERVER_1       "pool.ntp.org"
#define GUI_NTP_SERVER_2       "time.google.com"

// USB host peripheral: 0 = SDK default (High-Speed OTG on ESP32-P4).
// Set to (1 << 0) or (1 << 1) to force the first / second USB OTG peripheral.
#define GUI_USB_PERIPHERAL_MAP 0

// Display geometry (see lib/ATD5-P4/src/Common.h)
#define GUI_W                  800
#define GUI_H                  480
#define GUI_STATUS_H           36   // status bar height
#define GUI_TITLE_H            60   // title bar height
