/*
   Name:      scd30-mqtt-domoticz
   Git:       https://github.com/MartijnvdB/SCD30-MQTT-Domoticz
   Purpose:   Publishing Seedstudio SCD30 environmental data to Domoticz over MQTT.
              Uses the SCD30 library from Seedstudio.

              - values are read periodically from the SCD30
              - stored in a JSON object, with an identifier that matches the 'virtual hardware' device in Domoticz
              - values are published on an MQTT queue over WiFi
              - values are also displayed on a small OLED, together with:
                - MQTT connection status
                - WiFi connection status
                - NTP time
                The display is first built in a buffer and then displayed. But does not use callbacks. See library
                references, below.

              Requires that the applications has some knowledge of the device ID in Domoticz, this is configured
              in credentials.h.

              Custom OLED graphics defined in graphics.h.

              Uses custom logging library that, for now logs to the Serial console. Logging can be configured on
              a 'subsystem' level by setting the log level, like so:
              logger.SetLogLevel(S_SCD30, LOG_TRACE);


              ESP8266 pinout:
              SPI CL: D1
              SPI DA: D2

   Author:    Martijn van den Burg
   Device:    ESP8266
   Date:      Dec 2020/Jan 2021
   Changes:   1.1.0   display buffer for SSD1306 implemented
              1.2.0   self calibration enabled using external switch

*/



/*
  To be able to use ESP8266's API, e.g. for SLEEP modes.
  SDK headers are in C, include them extern or else they will throw an error compiling/linking
  all SDK .c files required can be added between the { }
*/
//#ifdef ESP8266
//extern "C" {
//#include "user_interface.h"
//}
//#endif



// Subfolder structure requires Arduino IDE 1.6.10 or up
#include "src/hardware.h"
#include "src/credentials.h"
#include "src/graphics.h"
#include "src/version.h"
#include "src/Logging/Logging.h"


#include <SparkFun_SCD30_Arduino_Library.h> // https://github.com/sparkfun/SparkFun_SCD30_Arduino_Library

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

// For NTP time
#include "src/DateTime/DateTime.h"
#include <ezTime.h>   // https://github.com/ropg/ezTime

// 128x64 OLED library
// SSD1306 by Alex Dynda, https://github.com/lexus2k/ssd1306
#include <ssd1306.h>
#include <nano_engine.h>  // https://lexus2k.github.io/ssd1306/md_nano_engine__r_e_a_d_m_e.html


#include <PubSubClient.h>
#include <Wire.h>


// Subsystem definitions
#define S_SCD30       0
#define S_PUBLISHER   1
#define S_WIFI        2

// Log level definitions
#define LOG_TRACE      0
#define LOG_DEBUG      1
#define LOG_INFO       2
#define LOG_WARN       3
#define LOG_ERROR      4

#define SECOND_TO_MILLIS 1000



/*
   ESP8266 pin definitions. See hardware.h.
*/
Mcu myboard = {
  16,     // on board LED, GPIO2
  12      // D6, GPIO12
};


// Struct to contain all application 'global' variables.
struct appConfig {
  const uint32_t pollTime_millis = 10000; // poll interval SCD30 sensor
  uint32_t prev_poll = 0;             // most recent poll time

  // Current measurements from the SCD30
  uint16_t cur_co2;
  float cur_temperature;
  float cur_humidity;

  // These are used to not send data when the values haven't changed.
  uint16_t prev_co2 = 0;
  float prev_humidity = 0.0;
  float prev_temperature = 0.0;

  // For NTP time
  uint32_t prev_timestamp = millis();
  char timeCast[9] = {};

  // Boolean for screen refresh
  bool doRefresh = true;
} app;


// Hardware settings. See hardware.h.
sSCD30 sensorhardware;

// OLED canvas size
const int canvasWidth = 128;
const int canvasHeight = 64;
uint8_t canvasData[canvasWidth * (canvasHeight / 8)];
NanoCanvas1 canvas(canvasWidth, canvasHeight, canvasData);  // https://lexus2k.github.io/ssd1306/class_nano_canvas1.html

// Instantiate a Logging object. Logger writes to the serial console
myns::Logging logger;


// For MQTT:
WiFiClient espClient;
PubSubClient client(espClient);


// Set the NTP timezone
Timezone AMS;

// SCD30 sensor
SCD30 airSensor;


void setup() {
  pinMode(myboard.set_auto_calibrate, INPUT_PULLUP);
  pinMode(myboard.onboard_LED, OUTPUT);

  digitalWrite(myboard.onboard_LED, HIGH);  // negative logic

  // Enable global logging
  logger.LogGlobalOn();

  // Set log levels for individual subsystems. Logging is to the IDE serial monitor.
  logger.SetLogLevel(S_SCD30, LOG_INFO);
  logger.SetLogLevel(S_PUBLISHER, LOG_INFO);
  logger.SetLogLevel(S_WIFI, LOG_INFO);

  /* OLED initialization and declarations */
  ssd1306_128x64_i2c_init();
  ssd1306_setFixedFont(ssd1306xled_font6x8);  // set small font

  // Display boot screen
  canvas.clear();
  canvas.printFixed(19, 1, "SCD30 data", STYLE_NORMAL);
  canvas.printFixed(86, 1, SKETCH_VERSION, STYLE_NORMAL);
  canvas.drawRect(0, 16, 127, 63);
  canvas.printFixed(19, 26, "M. van den Burg", STYLE_NORMAL);
  canvas.printFixed(40, 42, "May 2021", STYLE_NORMAL);
  canvas.blt(0, 0);
  delay(2000);
  canvas.clear();


  // Connect to WiFi
  wifi_connect();

  // Configure MQTT connection and connect.
  client.setServer(MQTT_SERVER, MQTT_PORT);
  mqttConnect();


  // Initialize SCD30
  logger.Log(S_SCD30, LOG_TRACE, "Initializing Wire and SCD30 sensor.\n");
  Wire.begin();
  while (airSensor.begin() == false) {
    logger.Log(S_SCD30, LOG_ERROR, "Initializing of SCD30 sensor failed.\n");
    displayAll();
    delay(1000);
  }

  // Fall through
  airSensor.setTemperatureOffset(sensorhardware.temp_offset); // temperature reading is high
  if (digitalRead(myboard.set_auto_calibrate) == LOW) {
    airSensor.setAutoSelfCalibration(true);
    //    digitalWrite(myboard.onboard_LED, LOW); // negative logic
  }
  else {
    airSensor.setAutoSelfCalibration(false);
  }


  // NTP date and time
  logger.Log(S_WIFI, LOG_TRACE, "Waiting for NTP tine sync.\n");
  waitForSync();
  AMS.setLocation("Europe/Amsterdam");

} // setup




void loop() {

  // Display HH:mm:ss time in display. Update every second, faster not needed.
  if ( (millis() - app.prev_timestamp) >= SECOND_TO_MILLIS ) {
    app.prev_timestamp = millis();
    AMS.dateTime("H:i:s").toCharArray(app.timeCast, sizeof(app.timeCast));  // cast String to char
    app.doRefresh = true;
  }

  // Read and display sensor values
  if (millis() - app.prev_poll > app.pollTime_millis) { // interrupt would be overkill
    logger.Log(S_SCD30, LOG_TRACE, "Poller fired.\n");
    app.prev_poll = millis();

    if (readSensor()) {
      publishMeasurements();
      app.doRefresh = true;
    }
  }


  // Refresh display when data has changed
  if (app.doRefresh) {
    displayAll();
    app.doRefresh = false;
  }

  // Give control to MCU for WiFi stuff
  yield();

} // loop


/* Initialize WiFi and get the time from NTP */
void wifi_connect() {
  char buffer[28] = {};

  logger.Log(S_WIFI, LOG_TRACE, "\nConnecting to WiFi\n");

  // HOSTNAME, SSID and PASSWORD are #defined in credentials.h
  wifi_station_set_hostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(MY_SSID, MY_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    logger.Log(S_WIFI, LOG_TRACE, ".");
    delay(200);
    displayAll();
  }

  logger.Log(S_WIFI, LOG_INFO, "\nWiFi connected\n");
  sprintf(buffer, "IP address: %s\n", WiFi.localIP().toString().c_str());
  logger.Log(S_WIFI, LOG_TRACE, buffer);
} // wifi_connect



/*
  Connect to MQTT
  Returns 1 for success, 0 for failure.
*/
int mqttConnect() {
  char buffer[40] = {};

  // Nothing to do if we're still connected.
  if (client.connected()) {
    logger.Log(S_WIFI, LOG_TRACE, "\nAlready connected to MQTT. Nothing to do.\n");
    return 1;
  }
  else {
    sprintf(buffer, "Not connected to MQTT. Reason: %d\n", client.state());
    logger.Log(S_WIFI, LOG_WARN, buffer);

    // Attempt to connect
    logger.Log(S_WIFI, LOG_TRACE, "Connecting to MQTT...\n");

    if (client.connect(CONNECTION_ID, CLIENT_NAME, CLIENT_PASSWORD)) {
      logger.Log(S_WIFI, LOG_INFO, "Connected to MQTT.\n");
      // Once connected, publish an announcement
      client.publish(STATUS_TOPIC, "SCD30 CO2 sensor has connected");
      return 1;
    }
    else {
      sprintf(buffer, "Failed to connected to MQTT. Reason: %d\n", client.state());
      client.publish(STATUS_TOPIC, buffer);
      logger.Log(S_WIFI, LOG_ERROR, buffer);
      return 0;
    }
  }

} // mqttConnect



/*
   Build and display the main canvas
*/
void displayAll() {
  char buffer[20] = {};

  canvas.clear();

  canvas.drawLine(0, 10, ssd1306_displayWidth() - 1, 10);
  // time
  if (strlen(app.timeCast) == 0) {
    canvas.printFixed(0, 0, "--:--:--", STYLE_NORMAL);
  }
  else {
    canvas.printFixed(0, 0, app.timeCast, STYLE_NORMAL);
  }
  // MQTT status
  if (! client.connected()) {
    canvas.printFixed(80, 0, "[----]", STYLE_NORMAL);
  }
  else {
    canvas.printFixed(80, 0, "[MQTT]", STYLE_NORMAL);
  }
  // WiFi status
  if (WiFi.status() == WL_CONNECTED) {
    canvas.drawBitmap1(120, 0, sizeof(wifiImage), sizeof(wifiImage), wifiImage);
  }
  else {
    // Create a blink effect while there's no WiFi
    if ( (millis() % 1000) < 500) {
      canvas.drawBitmap1(120, 0, sizeof(noWifiImage), sizeof(noWifiImage), noWifiImage);
    }
  }


  // measurements
  if (app.cur_co2 > 0) {
    ssd1306_setFixedFont(ssd1306xled_font8x16);   // set big font

    sprintf(buffer, "CO : %d ppm\0", app.cur_co2);
    canvas.printFixed(0, 13, buffer, STYLE_NORMAL);
    canvas.printFixed(16, 15, "2", STYLE_NORMAL);  // subscript
    memset(buffer, 0, sizeof buffer); // clear buffer

    if (airSensor.getAutoSelfCalibration() == false) { // not in calibration mode
      sprintf(buffer, "Temp.: %.1f C\0", app.cur_temperature);
      canvas.printFixed(0, 29, buffer, STYLE_NORMAL);
      memset(buffer, 0, sizeof buffer);
      sprintf(buffer, "Humidity: %.0f%%\0", app.cur_humidity);
      canvas.printFixed(0, 45, buffer, STYLE_NORMAL);
    }
    else {
      ssd1306_setFixedFont(ssd1306xled_font6x8);  // set small font
      sprintf(buffer, "Auto cal. on mode ON");
      canvas.printFixed(0, 31, buffer, STYLE_NORMAL);
      ssd1306_setFixedFont(ssd1306xled_font8x16);   // set big font
    }

    ssd1306_setFixedFont(ssd1306xled_font6x8);  // set small font

  }
  else {
    canvas.printFixed(16, 34, "Waiting for data", STYLE_NORMAL);
  }

  canvas.blt(0, 0);

} // displayAll


/*
   Read the SCD30 sensor.
   Return values:
   0: no data available
   1: data available
*/
int readSensor() {
  logger.Log(S_SCD30, LOG_INFO, "Function readSensor() entered.\n");

  char buffer[20] = {};

  if (airSensor.dataAvailable()) {
    logger.Log(S_SCD30, LOG_TRACE, "SCD30 sensor is available.\n");

    // Store values after rounding off to one decimal
    app.cur_co2 = airSensor.getCO2();
    app.cur_temperature = roundOff(airSensor.getTemperature());
    app.cur_humidity = roundOff(airSensor.getHumidity());

    sprintf(buffer, "CO2 concentration: %d ppm\n", app.cur_co2);
    logger.Log(S_SCD30, LOG_TRACE, buffer);
    sprintf(buffer, "Temperature: %.1f ℃\n", app.cur_temperature);
    logger.Log(S_SCD30, LOG_TRACE, buffer);
    sprintf(buffer, "Humidity: %.0f %%\n", app.cur_humidity);
    logger.Log(S_SCD30, LOG_TRACE, buffer);

    return 1;
  }
  else {
    logger.Log(S_SCD30, LOG_ERROR, "SCD30 sensor was not available to read from.\n");
  }

  // fall through
  return 0;
} // readSensor


/*
   Publish measured values to MQTT
*/
void publishMeasurements() {
  logger.Log(S_PUBLISHER, LOG_INFO, "Function publishMeasurements() entered.\n");

  char buffer[12] = {};

  char jsonoutput[128];
  DynamicJsonDocument thum(80);
  DynamicJsonDocument carb(48);


  /* We need to publish two objects because of the hardware settings in the Domoticz version:
    - CO2
    - temperature and humidity
  */
  // Check if data hase changed since last
  if ( app.cur_temperature != app.prev_temperature || app.cur_humidity != app.prev_humidity) {
    app.prev_temperature = app.cur_temperature;
    app.prev_humidity = app.cur_humidity;

    thum["name"] = "SCD30 Temp en Vocht";
    thum["idx"] = IDX_DEVICE_TEMPHUM;
    thum["nvalue"] = 0;

    sprintf(buffer, "%.1f;%.0f;1", app.cur_temperature, app.cur_humidity);

    if (! thum["svalue"].set(buffer) ) {
      logger.Log(S_PUBLISHER, LOG_ERROR, "Not enough space for buffer assignment\n");
    }
    serializeJson(thum, jsonoutput);

    logger.Log(S_PUBLISHER, LOG_TRACE, jsonoutput);
    logger.Log(S_PUBLISHER, LOG_TRACE, "\n");

    publishData(jsonoutput);
  }
  else {
    logger.Log(S_PUBLISHER, LOG_TRACE, "Temperature and humidity have not changed since last update. Not sending to Domoticz.\n");
  }

  if (app.cur_co2 != app.prev_co2) {
    app.prev_co2 = app.cur_co2;
    carb["name"] = "SCD30 CO2";
    carb["idx"] = IDX_DEVICE_CO2;
    carb["nvalue"] = app.cur_co2;
    serializeJson(carb, jsonoutput);

    logger.Log(S_PUBLISHER, LOG_TRACE, jsonoutput);
    logger.Log(S_PUBLISHER, LOG_TRACE, "\n");

    publishData(jsonoutput);
  }
  else {
    logger.Log(S_PUBLISHER, LOG_TRACE, "CO2 concentration has not changed since last update. Not sending to Domoticz.\n");
  }

  logger.Log(S_PUBLISHER, LOG_TRACE, "Done reading SCD30 sensor.\n\n");

} // publishMeasurements



/* publish a formatted message to MQTT
*/
void publishData(char* msg) {
  logger.Log(S_PUBLISHER, LOG_TRACE, "Function publishData() entered.\n");

  // Reconnect, if necessary.
  if (!client.connected()) {
    logger.Log(S_PUBLISHER, LOG_WARN, "Not connected to MQTT.\n");
    mqttConnect();
  }

  logger.Log(S_PUBLISHER, LOG_TRACE, "Publishing data to MQTT.\n");

  client.publish(TOPIC, msg);

  logger.Log(S_PUBLISHER, LOG_TRACE, "Done publishing data to MQTT.\n");

} // publishData



/* Round of floats to 1 decimal
   37.66666 * 10 = 376.666
   376.666 + 0.5 = 377.116    for rounding off value
   then type cast to int so value is 377
   then divided by 10 so the value converted into 37.7
*/
float roundOff(float var) {
  float value = (int)(var * 10 + 0.5);
  return (float)(value / 10);
}


/* END */
