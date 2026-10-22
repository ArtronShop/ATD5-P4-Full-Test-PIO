#include <Arduino.h>
#include <lvgl.h>
#include <ATD5-P4.h>
#include "gui/gui.h"

void setup() {
  Serial.begin(115200);

  // Setup peripherals
  Display.begin();
  Touch.begin();
  Sound.begin();

  // Map peripheral to LVGL
  Display.useLVGL(); // Map display to LVGL
  Touch.useLVGL(); // Map touch screen to LVGL
  Sound.useLVGL(); // Map speaker to LVGL (beep on touch)
  // The MicroSD Card is mounted by the SD test page (src/gui/gui_sd.cpp)

  Display.enableAutoSleep(120); // Auto off display if not touch in 2 min

  // Hardware test GUI (src/gui)
  gui_init();
}

void loop() {
  Display.loop(); // Keep GUI work
}
