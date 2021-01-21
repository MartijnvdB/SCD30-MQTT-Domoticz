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
*/



/*
  To be able to use ESP8266's API, e.g. for SLEEP modes.
  SDK headers are in C, include them extern or else they will throw an error compiling/linking
  all SDK .c files required can be added between the { }
*/
#ifdef ESP8266
extern "C" {
#include "user_interface.h"
}
#endif



// Subfolder structure requires Arduino IDE 1.6.10 or up
#include "src/hardware.h"
#include "src/credentials.h"
#include "src/graphics.h"
#include "src/Logging/Logging.h"


#include <SparkFun_SCD30_Arduino_Library.h> // https://github.com/sparkfun/SparkFun_SCD30_Arduino_Library

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

// For NTP time
#include "src/DateTime/DateTime.h"
#include <ezTime.h>   // https://github.com/ropg/ezTime

// 128x64 OLED library
// SSD1306 by Alex Dynda, https://github.com/lexus2k/ssd1306
//#include <nano_gfx.h>
#include <ssd1306.h>

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



// Struct to contain all application 'global' variables.
struct appConfig {
  const uint32_t pollTime_millis = 10000; // poll interval SCD30 sensor
  uint32_t previous_poll = 0;             // most recent poll time

  // These are used to not send data when the values haven't changed.
  uint16_t previous_co2 = 0;
  float previous_humidity = 0.0;
  float previous_temperature = 0.0;

  // For NTP time
  uint32_t previous_timestamp = millis();
  char timeCast[9];
} app;


// Hardware settings. See hardware.h.
sSCD30 sensorhardware;


// Instantiate a Logging object. Logger writes to the serial console
myns::Logging logger;



// For MQTT:
WiFiClient espClient;
PubSubClient client(espClient);


// Set the NTP timezone
Timezone AMS;

// SCD30 sensor
SCD30 airSensor;


// For OLED display
SPRITE wifisprite;


void setup() {
  // Enable global logging
  logger.LogGlobalOn();

  // Set log levels for individual subsystems
  logger.SetLogLevel(S_SCD30, LOG_INFO);
  logger.SetLogLevel(S_PUBLISHER, LOG_ERROR);
  logger.SetLogLevel(S_WIFI, LOG_INFO);

  /* OLED initialization and declarations */
  ssd1306_128x64_i2c_init();

  ssd1306_clearScreen();
  ssd1306_setFixedFont(ssd1306xled_font6x8);  // set small font
  ssd1306_printFixed(40, 1, "SCD30 data", STYLE_NORMAL);
  ssd1306_printFixed(20, 16, "M. van den Burg", STYLE_NORMAL);
  ssd1306_printFixed(30, 32, "januari 2021", STYLE_NORMAL);

  delay(2000);

  ssd1306_clearScreen();
  ssd1306_drawLine(0, 10, ssd1306_displayWidth() - 1, 10);

  // Custom sprite defined in graphics.h
  wifisprite = ssd1306_createSprite(120, 0, sizeof(wifiImage), wifiImage);

  // MQTT connection and time placeholders
  ssd1306_printFixed(0, 0, "--:--:--", STYLE_NORMAL);
  ssd1306_printFixed(80, 0, "[----]", STYLE_NORMAL);

  // Connect to WiFi
  wifi_connect();

  // Configure MQTT connection and connect.
  client.setServer(MQTT_SERVER, MQTT_PORT);
  mqttConnect();


  // Initialize SCD30
  logger.Log(S_SCD30, LOG_TRACE, "Initializing Wire and SCD30 sensor.\n");
  Wire.begin();
  if (airSensor.begin() == false) {
    logger.Log(S_SCD30, LOG_ERROR, "Initializing of SCD30 sensor failed.\n");
    while (1) {
      ;
    } // keep looping
  }
  // Fall through
  airSensor.setTemperatureOffset(sensorhardware.temp_offset); // temperature reading is high
  ssd1306_printFixed(0, 35, "Waiting for data...", STYLE_NORMAL);


  // NTP date and time
  logger.Log(S_WIFI, LOG_TRACE, "Waiting for NTP tine sync.\n");
  waitForSync();
  AMS.setLocation("Europe/Amsterdam");

} // setup




void loop() {

  // Display HH:mm:ss time in display. Update every second, faster not needed.
  if ( abs(millis() - app.previous_timestamp) >= SECOND_TO_MILLIS ) {
    app.previous_timestamp = millis();
    AMS.dateTime("H:i:s").toCharArray(app.timeCast, sizeof(app.timeCast));  // cast String to char
    ssd1306_printFixed(0, 0, app.timeCast, STYLE_NORMAL);
  }


  // Read sensor values, store in JSON, place on FIFO
  if (millis() - app.previous_poll > app.pollTime_millis) { // interrupt is overkill
    logger.Log(S_SCD30, LOG_TRACE, "Poller fired.\n");
    app.previous_poll = millis();
    readSensor();
  }

  yield();

} // loop


/* Initialize WiFi and get the time from NTP */
void wifi_connect() {
  char buffer[28];
  uint8_t spriteOn = 0;

  logger.Log(S_WIFI, LOG_TRACE, "\nConnecting to WiFi\n");

  // HOSTNAME, SSID and PASSWORD are #define-ed in credentials.h
  wifi_station_set_hostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(MY_SSID, MY_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    logger.Log(S_WIFI, LOG_TRACE, ".");

    if (spriteOn) {
      spriteOn = 0;
      wifisprite.erase();
    }
    else {
      spriteOn = 1;
      wifisprite.draw();
    }
    delay(200);
  }

  wifisprite.draw();  // sprite on
  logger.Log(S_WIFI, LOG_INFO, "\nWiFi connected\n");

  sprintf(buffer, "IP address: %s\n", WiFi.localIP().toString().c_str());
  logger.Log(S_WIFI, LOG_TRACE, buffer);
} // wifi_connect



/*
  Connect to MQTT
  Returns 1 for success, 0 for failure.
*/
int mqttConnect() {
  char buffer[40];

  // Nothing to do if we're still connected.
  if (client.connected()) {
    logger.Log(S_WIFI, LOG_TRACE, "\nAlready connected to MQTT. Nothing to do.\n");
    return 1;
  }
  else {
    ssd1306_printFixed(80, 0, "[----]", STYLE_NORMAL);

    sprintf(buffer, "Not connected to MQTT. Reason: %d\n", client.state());
    logger.Log(S_WIFI, LOG_WARN, buffer);

    // Attempt to connect
    logger.Log(S_WIFI, LOG_TRACE, "Connecting to MQTT...\n");
    if (client.connect(CONNECTION_ID, CLIENT_NAME, CLIENT_PASSWORD)) {
      logger.Log(S_WIFI, LOG_INFO, "Connected to MQTT.\n");

      ssd1306_printFixed(80, 0, "[MQTT]", STYLE_NORMAL);

      // Once connected, publish an announcement
      client.publish(STATUS_TOPIC, "CO2 sensor has connected");
      return 1;
    }
    else {
      ssd1306_printFixed(80, 0, "[XXXX]", STYLE_NORMAL);
      sprintf(buffer, "Failed to connected to MQTT. Reason: %d\n", client.state());
      client.publish(STATUS_TOPIC, buffer);
      logger.Log(S_WIFI, LOG_ERROR, buffer);
      return 0;
    }
  }

} // mqttConnect


void readSensor() {
  logger.Log(S_SCD30, LOG_TRACE, "Function readSensor() entered.\n");

  char buffer[60];
  buffer[0] = '\0'; // make array appear empty
  
  char jsonoutput[128];
  DynamicJsonDocument thum(64);
  DynamicJsonDocument carb(48);

  if (airSensor.dataAvailable()) {
    logger.Log(S_SCD30, LOG_TRACE, "SCD30 sensor is available.\n");

    // Store values after rounding off to one decimal
    uint16_t cur_co2 = airSensor.getCO2();
    float cur_temp = round(airSensor.getTemperature());
    float cur_humidity = round(airSensor.getHumidity());

    sprintf(buffer, "CO2 concentration: %d ppm\n", cur_co2);
    logger.Log(S_SCD30, LOG_TRACE, buffer);
    sprintf(buffer, "Temperature: %.1f ℃\n", cur_temp);
    logger.Log(S_SCD30, LOG_TRACE, buffer);
    sprintf(buffer, "Humidity: %.0f %%\n", cur_humidity);
    logger.Log(S_SCD30, LOG_TRACE, buffer);

    // Print to display
    sprintf(buffer, "CO2: %d ppm", cur_co2);
    ssd1306_printFixed(0, 20, buffer, STYLE_NORMAL);
    sprintf(buffer, "Temperature: %.1f C", cur_temp);
    ssd1306_printFixed(0, 35, buffer, STYLE_NORMAL);
    sprintf(buffer, "Humidity: %.0f%%", cur_humidity);
    ssd1306_printFixed(0, 50, buffer, STYLE_NORMAL);

    /* We need to publish two objects because of the hardware settings in the Domoticz version:
      - CO2
      - temperature and humidity
    */
    logger.Log(S_SCD30, LOG_TRACE, "Serializing SCD30 sensor data to JSON.\n");

    // Check if data hase changed since last
    if ( cur_temp != app.previous_temperature || cur_humidity != app.previous_humidity) {
      app.previous_temperature = cur_temp;
      app.previous_humidity = cur_humidity;

      thum["name"] = "SCD30 Temp en Vocht";
      thum["idx"] = IDX_DEVICE_TEMPHUM;
      thum["nvalue"] = 0;
      //      sprintf(buffer, "%.1f;%.0f;1", result[1], result[2]);
      sprintf(buffer, "%.1f;%.0f;1", cur_temp, cur_humidity);
      thum["svalue"].set(buffer); // does not work with '=' assignment...
      serializeJson(thum, jsonoutput);

      logger.Log(S_SCD30, LOG_TRACE, jsonoutput);
      logger.Log(S_SCD30, LOG_TRACE, "\n");

      publishData(jsonoutput);
    }
    else {
      logger.Log(S_SCD30, LOG_TRACE, "Temperature and humidity have not changed since last update. Not sending to Domoticz.\n");
    }

    if (cur_co2 != app.previous_co2) {
      app.previous_co2 = cur_co2;
      carb["name"] = "SCD30 CO2";
      carb["idx"] = IDX_DEVICE_CO2;
      carb["nvalue"] = cur_co2;
      serializeJson(carb, jsonoutput);

      logger.Log(S_SCD30, LOG_TRACE, jsonoutput);
      logger.Log(S_SCD30, LOG_TRACE, "\n");

      publishData(jsonoutput);
    }
    else {
      logger.Log(S_SCD30, LOG_TRACE, "CO2 concentration has not changed since last update. Not sending to Domoticz.\n");
    }

  }
  else {
    logger.Log(S_SCD30, LOG_ERROR, "SCD30 sensor was not available to read from.\n");
  }
  logger.Log(S_SCD30, LOG_TRACE, "Done reading SCD30 sensor.\n");

} // readSensor



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
float round(float var) {
  float value = (int)(var * 10 + 0.5);
  return (float)value / 10;
}


/* END */
