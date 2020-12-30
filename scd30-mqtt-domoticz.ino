/*
   Name:      scd30-mqtt-domoticz
   Git:       https://github.com/MartijnvdB/SCD30-MQTT-Domoticz
   Purpose:   Publishing Seedstudio SCD30 environmental data to Domoticz over MQTT.
              Uses the SCD30 library from Seedstudio.

              - values are read periodically from the SCD30
              - stored in a JSON object, with an identifier that matches the 'virtual hardware' device in Domoticz
              - values are published on an MQTT queue over WiFi

              Requires that the applications has some knowledge of the device ID in Domoticz.

              Uses custom logging library that, for now logs to the Serial console. Logging can be configured on 
              a 'subsystem' level by setting the log level, like so:
              logger.SetLogLevel(S_SCD30, LOG_TRACE);


              ESP8266 pinout:
              SPI CL: D1
              SPI DA: D2

   Author:    Martijn van den Burg
   Device:    ESP8266
   Date:      Dec 2020
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
#include "src/Logging/Logging.h"


#include "SCD30.h"  // Seeedstudio library
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
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


// Struct to contain all application 'global' variables.
struct appConfig {
  const uint32_t pollTime_millis = 10000; // poll interval SCD30 sensor
  uint32_t previous_poll = 0;             // most recent poll time
} app;



// Instantiate a Logging object. Logger writes to the serial console
myns::Logging logger;



// For MQTT:
WiFiClient espClient;
PubSubClient client(espClient);


void setup() {
  // Enable global logging
  logger.LogGlobalOn();

  // Set log levels for individual subsystems
  logger.SetLogLevel(S_SCD30, LOG_TRACE);
  logger.SetLogLevel(S_PUBLISHER, LOG_TRACE);
  logger.SetLogLevel(S_WIFI, LOG_INFO);

  // Connect to WiFi
  wifi_connect();

  // Configure MQTT connection and connect.
  client.setServer(MQTT_SERVER, MQTT_PORT);
  mqttConnect();


  // Initialize SCD30
  logger.Log(S_SCD30, LOG_TRACE, "Initializing Wire and SCD30 sensor.\n");
  Wire.begin();
  scd30.initialize();


} // setup




void loop() {

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

  logger.Log(S_WIFI, LOG_TRACE, "\nConnecting to WiFi\n");

  // HOSTNAME, SSID and PASSWORD are #define-ed in credentials.h
  wifi_station_set_hostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(MY_SSID, MY_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    logger.Log(S_WIFI, LOG_TRACE, ".");
    delay(200);
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
  char buffer[40];

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
      client.publish(STATUS_TOPIC, "CO2 sensor has connected");
      return 1;
    }
    else {
      sprintf(buffer, "Failed to connected to MQTT. Reason: %d\n", client.state());
      logger.Log(S_WIFI, LOG_ERROR, buffer);
      return 0;
    }
  }

} // mqttConnect


void readSensor() {
  logger.Log(S_SCD30, LOG_TRACE, "Function readSensor() entered.\n");

  float result[3] = {0};
  char buffer[60];
  char jsonoutput[128];
  DynamicJsonDocument thum(64);
  DynamicJsonDocument carb(48);

  if (scd30.isAvailable()) {
    logger.Log(S_SCD30, LOG_TRACE, "SCD30 sensor is available.\n");
    scd30.getCarbonDioxideConcentration(result);

    sprintf(buffer, "CO2 concentration: %.0f ppm\n", result[0]);
    logger.Log(S_SCD30, LOG_TRACE, buffer);
    sprintf(buffer, "Temperature: %.1f ℃\n", result[1]);
    logger.Log(S_SCD30, LOG_TRACE, buffer);
    sprintf(buffer, "Humidity: %.0f %%\n", result[2]);
    logger.Log(S_SCD30, LOG_TRACE, buffer);

    /* We need to publish two objects because of the hardware settings in the Domoticz version:
      - CO2
      - temperature and humidity
    */
    logger.Log(S_SCD30, LOG_TRACE, "Serializing SCD30 sensor data to JSON.\n");

    thum["name"] = "SCD30 Temp en Vocht";
    thum["idx"] = IDX_DEVICE_TEMPHUM;
    thum["nvalue"] = 0;
    sprintf(buffer, "%.1f;%.0f;1", result[1], result[2]);
    thum["svalue"].set(buffer); // does not work with '=' assignment...
    serializeJson(thum, jsonoutput);

    logger.Log(S_SCD30, LOG_TRACE, jsonoutput);

    publishData(jsonoutput);


    carb["name"] = "SCD30 CO2";
    carb["idx"] = IDX_DEVICE_CO2;
    carb["nvalue"] = (uint16_t)result[0];
    serializeJson(carb, jsonoutput);

    logger.Log(S_SCD30, LOG_TRACE, jsonoutput);

    publishData(jsonoutput);

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
