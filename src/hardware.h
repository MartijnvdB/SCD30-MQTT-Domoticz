/*
   Name:    hardware.h
   Purpose: Device and associated hardware definitions.
   Date:    Dec 2020
*/


// MCU pin definitions
struct Esp8266 {
  const uint8_t onboard_LED;
};


// Electronics outside of the ESP8266
struct Hardware {
  const uint8_t CLK;    // SCD30 I2C CLK
  const uint8_t DIO;    // SCD30 I2C DIO
};
