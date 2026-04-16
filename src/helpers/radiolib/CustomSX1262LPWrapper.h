#pragma once

#include "CustomSX1262LP.h"
#include "CustomSX1262Wrapper.h"
#include "RadioLibWrappers.h"
#include "SX126xReset.h"

#ifndef USE_SX1262
#define USE_SX1262
#endif

class CustomSX1262LPWrapper : public CustomSX1262Wrapper {
public:
  CustomSX1262LPWrapper(CustomSX1262LP& radio, mesh::MainBoard& board) : CustomSX1262Wrapper(radio, board) { }

  void setLowPower(bool lp, uint16_t preamble=0, uint16_t maxbytes=0) {
    ((CustomSX1262LP *)_radio)->setLowPower(lp, preamble, maxbytes);
  }
};
