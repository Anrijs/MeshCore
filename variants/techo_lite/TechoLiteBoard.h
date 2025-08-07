#pragma once

#include <MeshCore.h>
#include <Arduino.h>

// RF  ---  ---  ---   D    D    D
// TX  1.8  1.6  0.0  1.8  1.6  0.0
//     err               


class TechoLiteBoard : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  uint8_t btn_prev_state;

public:
  void begin();

  uint8_t getStartupReason() const override { return startup_reason; }

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);
    digitalWrite(PIN_VBAT_READ_EN, HIGH);
    analogReference(AR_INTERNAL_3_0);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }

    digitalWrite(PIN_VBAT_READ_EN, LOW);
    raw = raw / BATTERY_SAMPLES;

    Serial.printf("ADC Measure: raw=%u, vlotage=%.2f\n", raw, ADC_MULTIPLIER * raw);
    return (ADC_MULTIPLIER * raw);
  }

  const char* getManufacturerName() const override {
    return "T-Echo Lite";
  }

  int buttonStateChanged() {
    #ifdef PIN_USER_BTN
      uint8_t v = digitalRead(PIN_USER_BTN);
      if (v != btn_prev_state) {
        btn_prev_state = v;
        return (v == LOW) ? 1 : -1;
      }
    #endif
      return 0;
  }

  void reboot() override {
    NVIC_SystemReset();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;
};
