#ifndef NUSA_PINOUT_H
#define NUSA_PINOUT_H

// Target hardware: classic ESP32 DevKit / WROOM-32.
// RX uses ADC1_CH0 = GPIO36. TX uses internal DAC channel 1 = GPIO26.
#define AFSK_RX_PIN       36
#define AFSK_TX_DAC_PIN   26
#define PTT_PIN_DEFAULT   25
#define RX_LED_PIN_DEFAULT 2
#define TX_LED_PIN_DEFAULT 4

// Kept for compatibility with the uploaded AFSK.h macros.
#ifndef RX_LED_PIN
#define RX_LED_PIN RX_LED_PIN_DEFAULT
#endif

#endif
