#pragma once

#include <RadioLib.h>
#include "CustomSX1262.h"

class CustomSX1262LP : public CustomSX1262 {
  bool lowPower = false;
  uint16_t preamble = 0;
  uint16_t minSymbols = 0;

  public:
    CustomSX1262LP(Module *mod) : CustomSX1262(mod) { }

    // override startReceive with LP
    void setLowPower(bool lp, uint16_t preamble, uint16_t minSymbols) {
      this->lowPower = lp;
      this->preamble = preamble;
      this->minSymbols = minSymbols;
    }

    int16_t startReceive() {
      if (lowPower) {
        return CustomSX1262::startReceiveDutyCycleAuto(preamble, minSymbols);
      }
      return CustomSX1262::startReceive();
    }
};