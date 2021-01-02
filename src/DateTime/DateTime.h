/*
  DateTime.h

  Copyright 2019 Martijn van den Burg, martijn@[remove-me-first]palebluedot . nl

  This file is part of the DateTime library for parsing ISO8601 time strings on Nodemcu.

  DateTime is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free
  Software Foundation, either version 3 of the License, or (at your option)
  any later version.

  This software is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.

*/


/* This library is written for the Nodemcu and Arduino software version 1.8.9.

   This library may work with other hardware and/or software. YMMV.
*/

#include "Arduino.h"


#ifndef DateTime_h
#define DateTime_h

#define VERSION 1.0


class DateTime {
  public:
    DateTime(const char*);
    ~DateTime();

    char* dateonly();
    char* timeonly();
    uint16_t year();
    uint8_t month();
    uint8_t day();
    uint8_t hour();
    uint8_t minute();
    uint8_t second();

  private:
    char _dateonly[11];
    char _timeonly[9];
    uint16_t _year;
    uint8_t _month;
    uint8_t _day;
    uint8_t _hour;
    uint8_t _minute;
    uint8_t _second;
};


#endif
