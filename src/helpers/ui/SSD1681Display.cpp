
#include "SSD1681Display.h"

bool SSD1681Display::begin() {
  SPI1.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  SPI1.begin();

  display.begin();
  display.setRotation(DISPLAY_ROTATION);
  display.fillScreen(EPD_WHITE);
  display.clearBuffer();

  _init = true;
  return true;
}

void SSD1681Display::turnOn() {
  if (!_init) begin();
  _isOn = true;
}

void SSD1681Display::turnOff() {
  _isOn = false;
}

void SSD1681Display::clear() {
  display.fillScreen(EPD_WHITE);
  display.setTextColor(EPD_BLACK);
}

void SSD1681Display::startFrame(Color bkg) {
 display.fillScreen(EPD_WHITE);
}

void SSD1681Display::setTextSize(int sz) {
  switch(sz) {
    case 1:  // Small
      display.setFont(&FreeSans9pt7b);
      break;
    case 2:  // Medium Bold
      display.setFont(&FreeSansBold12pt7b);
      break;
    case 3:  // Large
      display.setFont(&FreeSans18pt7b);
      break;
    default:
      display.setFont(&FreeSans9pt7b);
      break;
  }
}

void SSD1681Display::setColor(Color c) {
 display.setTextColor(EPD_BLACK);
}

void SSD1681Display::setCursor(int x, int y) {
 display.setCursor(x*SCALE_X, (y+10)*SCALE_Y);
}

void SSD1681Display::print(const char* str) {
 display.print(str);
}

void SSD1681Display::fillRect(int x, int y, int w, int h) {
 display.fillRect(x*SCALE_X, y*SCALE_Y, w*SCALE_X, h*SCALE_Y, EPD_BLACK);
}

void SSD1681Display::drawRect(int x, int y, int w, int h) {
 display.drawRect(x*SCALE_X, y*SCALE_Y, w*SCALE_X, h*SCALE_Y, EPD_BLACK);
}

void SSD1681Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  // Calculate the base position in display coordinates
  uint16_t startX = x * SCALE_X;
  uint16_t startY = y * SCALE_Y;
  
  // Width in bytes for bitmap processing
  uint16_t widthInBytes = (w + 7) / 8;
  
  // Process the bitmap row by row
  for (uint16_t by = 0; by < h; by++) {
    // Calculate the target y-coordinates for this logical row
    int y1 = startY + (int)(by * SCALE_Y);
    int y2 = startY + (int)((by + 1) * SCALE_Y);
    int block_h = y2 - y1;
    
    // Scan across the row bit by bit
    for (uint16_t bx = 0; bx < w; bx++) {
      // Calculate the target x-coordinates for this logical column
      int x1 = startX + (int)(bx * SCALE_X);
      int x2 = startX + (int)((bx + 1) * SCALE_X);
      int block_w = x2 - x1;
      
      // Get the current bit
      uint16_t byteOffset = (by * widthInBytes) + (bx / 8);
      uint8_t bitMask = 0x80 >> (bx & 7);
      bool bitSet = pgm_read_byte(bits + byteOffset) & bitMask;
      
      // If the bit is set, draw a block of pixels
      if (bitSet) {
        // Draw the block as a filled rectangle
        display.fillRect(x1, y1, block_w, block_h, EPD_BLACK);
      }
    }
  }
}

uint16_t SSD1681Display::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w / SCALE_X;
}

void SSD1681Display::endFrame() {
  display.display();
}
