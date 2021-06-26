/*
   Name:    hardware.h
   Purpose: Device and associated hardware definitions.
   Date:    Dec 2020/Jan 2021
*/


struct sSCD30 {
   int16_t temp_offset = 2;
};



// MCU pin definitions
struct Mcu {
  const uint8_t onboard_LED;
  const uint8_t set_auto_calibrate;     // switch to ground to enable auto calibrate
};

