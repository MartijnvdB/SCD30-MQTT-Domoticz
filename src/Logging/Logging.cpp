/*
  Name:     Logging.cpp
  Purpose:  Source file for the Logging class
  Author:   MBURG
  Date:     Dec 2020
*/

#include "Arduino.h"
#include "Logging.h"


namespace myns {

// Constructor
Logging::Logging() {
  Serial.begin(DEFAULT_SERIAL_SPEED);
  Logging::LogGlobalOn();
}

Logging::~Logging() {}


void Logging::SetLogLevel(int subsys, uint16_t lvl) {
  logLevelConfig[subsys] = lvl;
}


uint16_t Logging::GetLogLevel(uint16_t subsys) {
  return logLevelConfig[subsys];
}

void Logging::Log(uint16_t subsys, uint16_t lvl, const char * msg) {
  if (Logging::LogGlobalState()) {  // logging globally enabled
    // log level of message is at least the level that's configured for the subsystem
    if (Logging::GetLogLevel(subsys) <= lvl) {
      Serial.print(msg);
    }
  }
  return;
}

void Logging::Log(uint16_t subsys, uint16_t lvl, const char * msg, int value) {

}


void Logging::LogGlobalOff() {
  logGlobal = 0;
}

void Logging::LogGlobalOn() {
  logGlobal = 1;
}

uint16_t Logging::LogGlobalState() {
  return logGlobal;
}


} // namespace
/* END */
