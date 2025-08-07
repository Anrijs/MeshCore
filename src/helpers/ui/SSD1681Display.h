#pragma once

#include <SPI.h>
#include <Adafruit_EPD.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

#include "DisplayDriver.h"

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 1
#endif

#if (DISPLAY_ROTATION == 1) || (DISPLAY_ROTATION == 3)
  #define SCALE_X   (DISP_HEIGHT / 128.0)
  #define SCALE_Y   (DISP_WIDTH  / 128.0)
#else
  #define SCALE_X   (DISP_WIDTH  / 128.0)
  #define SCALE_Y   (DISP_HEIGHT / 128.0)
#endif


class SSD1681Display : public DisplayDriver {
  Adafruit_SSD1681 display;

  bool _init = false;
  bool _isOn = false;

  public:
  // there is a margin in y...
    SSD1681Display() : DisplayDriver(128, 128), display(DISP_WIDTH, DISP_HEIGHT, DISP_DC, DISP_RST,
      DISP_CS, DISP_SRAM_CS, DISP_BUSY, &SPI1) {  }

  bool begin();
  bool isOn() override {return _isOn;};
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
};
