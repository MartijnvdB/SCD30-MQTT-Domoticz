/*
   Name:    hardware.h
   Purpose: Device and associated hardware definitions.
   Date:    Dec 2020
*/


struct sSCD30 {
   int16_t scd30_temp_offset = 200;	// in steps of 0.01 degC, only positive numbers allowed
};
