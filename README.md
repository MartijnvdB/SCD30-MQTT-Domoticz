# SCD30-MQTT-Domoticz

# Introduction #

This project is written for the ESP8266 using the Arduino IDE.

The ESP8266 reads environmental data (CO2 concentration, relative humidity, temperature) from an SCD30 sensor (using the Seeedstudio SCD30 library) and sends it to Mosquitto MQTT over WiFi for storage and display in Domoticz.

The data is also displayed on a tiny OLED, together with WiFi and MQTT connection status, and time (from NTP).
