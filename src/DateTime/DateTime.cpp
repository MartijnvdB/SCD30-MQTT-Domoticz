/* Name:    DateTime
   Purpose: Object for splitting an ISO8601 string into separate date/time parts.
   Author:  Martijn van den Burg, martijn@[remove-me-first]palebluedot . nl

   Copyright (C) 2019 Martijn van den Burg. All right reserved.

   This program is free software: you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation, either version 3 of the License, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "Arduino.h"
#include "DateTime.h"


/*
   Constructor.
   Takes as argument the ISO8601 datetimestamp (e.g. "2019-06-08T13:52:53+0200")
   and stores the individual components as integers rather than as strings.
*/
DateTime::DateTime(const char* datetime) {
  char y[5], M[3], d[3], h[3], m[3], s[3];  // temporary vars

   // date stuff
  strncpy(_dateonly, &datetime[0], 10);
  _dateonly[10] = '\0'; // string terminator
  strncpy(y, &_dateonly[0], 4);
  y[4] = '\0';
  strncpy(M, &_dateonly[5], 2);
  M[2] = '\0';
  strncpy(d, &_dateonly[8], 2);
  d[2] = '\0';

     // time stuff, HH:mm
  strncpy(_timeonly, &datetime[11], 5);  // copy from 'T' in the timestamp
  _timeonly[5] = '\0';

  strncpy(h, &datetime[11], 2);
  h[2] = '\0'; // terminator
  strncpy(m, &datetime[14], 2);
  m[2] = '\0';
  strncpy(s, &datetime[17], 2);
  s[2] = '\0';

  // convert char* to int
  _year = atoi(y);    // 19, but also '05' -> 5
  _month = atoi(M);
  _day = atoi(d);
  _hour = atoi(h);
  _minute = atoi(m);
  _second = atoi(s);
  
} // constructor


/*
   Destructor
*/
DateTime::~DateTime() {
  /* nothing to do */
}


/*
   Accessors
*/
char* DateTime::dateonly() {
  return _dateonly;
}
char* DateTime::timeonly() {
  return _timeonly;
}
uint16_t DateTime::year() {
  return _year;
}
uint8_t DateTime::month() {
  return _month;
}
uint8_t DateTime::day() {
  return _day;
}
uint8_t DateTime::hour() {
  return _hour;
}
uint8_t DateTime::minute() {
  return _minute;
}
uint8_t DateTime::second() {
  return _second;
}



/* End */
