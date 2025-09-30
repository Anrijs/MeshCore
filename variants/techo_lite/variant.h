 /*
  Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.
  Copyright (c) 2018, Adafruit Industries (adafruit.com)

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _VARIANT_T_ECHO_LITE_NRF52840_
#define _VARIANT_T_ECHO_LITE_NRF52840_

/** Master clock frequency */
#define VARIANT_MCK       (64000000ul)

#define USE_LFXO      // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

#define _PINNUM(port, pin) ((port) * 32 + (pin))

// Number of pins defined in PinDescription array
#define PINS_COUNT           (48)
#define NUM_DIGITAL_PINS     (48)
#define NUM_ANALOG_INPUTS    (8)
#define NUM_ANALOG_OUTPUTS   (0)

// LEDs
#define PIN_LED1             (_PINNUM(1,  7))
#define PIN_LED2             (_PINNUM(1,  5))

#define LED_GREEN            PIN_LED1
#define LED_BLUE             PIN_LED2

#define LED_BUILTIN          LED_GREEN
#define LED_CONN             LED_BLUE

#define LED_STATE_ON         0         // State when LED is litted

/*
 * Buttons
 */
#define PIN_BUTTON1          (_PINNUM(0, 24))

/*
 * Analog pins
 */
#define ADC_RESOLUTION    12

// Other pins
#define PIN_NFC1           (_PINNUM(0,  9))
#define PIN_NFC2           (_PINNUM(0, 10))

/*
 * Battery
 */
#define  PIN_VBAT_READ    (_PINNUM(0, 2))
#define  PIN_VBAT_READ_EN (_PINNUM(0, 31))
#define  ADC_MULTIPLIER   (7.90f)


/*
 * Serial interfaces
 */

#define PIN_SERIAL1_RX      (_PINNUM(1,  10))
#define PIN_SERIAL1_TX      (_PINNUM(0,  29))

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

// LoRa
#define PIN_SPI_MISO        (_PINNUM(0, 17))
#define PIN_SPI_MOSI        (_PINNUM(0, 15))
#define PIN_SPI_SCK         (_PINNUM(0, 13))

// Display
#define PIN_SPI1_MISO       -1
#define PIN_SPI1_MOSI       (_PINNUM(1, 15))
#define PIN_SPI1_SCK        (_PINNUM(1, 11))

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 2

// bottom on epaper
#define PIN_WIRE_SDA         (_PINNUM(1,  4))
#define PIN_WIRE_SCL         (_PINNUM(1,  2))
#define PIN_BOARD_SDA        PIN_WIRE_SDA
#define PIN_BOARD_SCL        PIN_WIRE_SCL

// top on epaper
#define PIN_WIRE1_SDA        (_PINNUM(0, 23))
#define PIN_WIRE1_SCL        (_PINNUM(0, 25))

// QSPI Pins
#define PIN_QSPI_SCK         (_PINNUM(0, 4))
#define PIN_QSPI_CS          (_PINNUM(0, 12))
#define PIN_QSPI_IO0         (_PINNUM(0,  6))
#define PIN_QSPI_IO1         (_PINNUM(0,  8))
#define PIN_QSPI_IO2         (_PINNUM(1,  9))
#define PIN_QSPI_IO3         (_PINNUM(0, 26))

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES   MX25R6435F
#define EXTERNAL_FLASH_USE_QSPI

// RT9080
#define RT9080_EN    (_PINNUM(0, 30))

// SX1262 
#define P_LORA_NSS   (_PINNUM(0, 11))
#define P_LORA_DIO_1 (_PINNUM(1,  8))
#define P_LORA_DIO_2 (_PINNUM(0,  5))
#define P_LORA_RESET (_PINNUM(0,  7))
#define P_LORA_BUSY  (_PINNUM(0, 14))
#define P_LORA_MISO  PIN_SPI_MISO
#define P_LORA_SCLK  PIN_SPI_SCK 
#define P_LORA_MOSI  PIN_SPI_MOSI
#define SX126X_RXEN  (_PINNUM(1,  1)) // VC2
#define SX126X_TXEN  (_PINNUM(0, 27)) // VC1
#define SX126X_DIO2_AS_RF_SWITCH false
#define SX126X_DIO3_TCXO_VOLTAGE (1.8f)


#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
