#include <FS.h> // needs to be first
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPIFFS.h>
#include <ArduinoJson.h> // v6+
#include <Audio.h> // https://github.com/schreibfaul1/ESP32-audioI2S
#include <time.h>

#define FORMAT_SPIFFS_IF_FAILED true
#define FILTER_LENGTH 50
#define LOOP_DELAY 10

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_username[15];
char mqtt_password[15];
char mqtt_topic[30] = "bedroom/alarm_clock";
char radio_station[100] = "";
char radio_volume[4] = "8";
char clock_gmt_offset[10] = "3600";
char clock_daylight_offset[10] ="3600";

//flag for saving data
bool shouldSaveConfig = false;

//sensor variables
const int clockPinClk = 4;
const int clockPinData = 5;
const int audioPinBClk = 36;
const int audioPinData = 44;
const int audioPinLRClk = 35;
const int volumeUpPin = 8;
const int volumeDownPin = 7;
const int playPin = 18;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

// audio
Audio audio;

//mqtt client
WiFiClient espClient;
PubSubClient client(espClient);

// mqtt reconnect
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("connected");

      // send hello message
      StaticJsonDocument<128> json;
      json["hello"] = 1;
      char mqtt_message[128];
      serializeJson(json, mqtt_message);
      publishMessage(mqtt_topic, mqtt_message, true);

      // subscribe to donfig
      char mqtt_sub_topic[40];
      const char* mqtt_config= "/config";
      strcpy(mqtt_sub_topic, mqtt_topic ); 
      strcat(mqtt_sub_topic, mqtt_config);
      client.subscribe(mqtt_sub_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// mqtt publish message
void publishMessage(const char* topic, String payload, boolean retained) {
  if (client.publish(topic, payload.c_str(), true))
    Serial.println("Message publised [" + String(topic) + "]: " + payload);
}

// mqtt call back for received messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String incommingMessage = "";
  for (int i = 0; i < length; i++) incommingMessage += (char)payload[i];

  Serial.println("Message arrived [" + String(topic) + "]: " + incommingMessage);

  // check for sensor_delay
  DynamicJsonDocument json(256);
  auto deserializeError = deserializeJson(json, incommingMessage);
  if ( ! deserializeError ) {
    // not implemented (copied from my other script)
    char new_volume[4];
    sprintf(new_volume, "%d", json["radio_volume"].as<unsigned int>()); // int to string conversion
    // sprintf(new_delay, "%d", json["sensor_delay"].as<unsigned int>()); // int to string conversion
    // sprintf(new_delay, "%d", json["sensor_delay"].as<unsigned int>()); // int to string conversion
    Serial.println(new_volume);
    if ( new_volume != "0" ) { // strlen handles if property even exists
      strcpy(radio_volume, new_volume);
      audio.setVolume(atoi(radio_volume));
      saveConfig();
    }
  } else {
    Serial.println("failed to load json config");
  }
}

void saveConfig() {
  Serial.println("saving config");
  DynamicJsonDocument json(1024);
  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"] = mqtt_port;
  json["mqtt_username"] = mqtt_username;
  json["mqtt_password"] = mqtt_password;
  json["mqtt_topic"] = mqtt_topic;
  json["radio_station"] = radio_station;
  json["radio_volume"] = radio_volume;
  json["clock_gmt_offset"] = clock_gmt_offset;
  json["clock_daylight_offset"] = clock_daylight_offset;

  File configFile = SPIFFS.open("/config.json", "w");
  if (configFile) {
    // serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
  } else {
    Serial.println("failed to open config file for writing");
  }
}

void setup() {
  // initialize serial
  Serial.begin(115200);
  while (!Serial)
    delay(10);
  Serial.println("Serial online");

  // initialize sonsor pins and sensor struct
  pinMode(clockPinClk, OUTPUT);
  pinMode(clockPinData, OUTPUT);
  pinMode(audioPinBClk, OUTPUT);
  pinMode(audioPinData, OUTPUT);
  pinMode(audioPinLRClk, OUTPUT);
  pinMode(volumeUpPin, INPUT_PULLUP);
  pinMode(volumeDownPin, INPUT_PULLUP);
  pinMode(playPin, INPUT_PULLUP);

  //clean FS, for testing
  // SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);
          strcpy(radio_station, json["radio_station"]);
          strcpy(radio_volume, json["radio_volume"]);
          strcpy(clock_gmt_offset, json["clock_gmt_offset"]);
          strcpy(clock_daylight_offset, json["clock_daylight_offset"]);
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6, "pattern='\\d{4,5}'");
  WiFiManagerParameter custom_mqtt_username("username", "mqtt username", mqtt_username, 15);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 15,"type='password'");
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 30);
  WiFiManagerParameter custom_radio_station("radio_station", "radio station", radio_station, 100);
  WiFiManagerParameter custom_radio_volume("radio_volume", "radio volume", radio_volume, 4);
  WiFiManagerParameter custom_clock_gmt_offset("gmt_offset", "clock timezone offset in seconds", clock_gmt_offset, 10);
  WiFiManagerParameter custom_clock_daylight_offset("daylight_offset", "daylight offset", clock_daylight_offset, 10);

  //WiFiManager
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_topic);
  wifiManager.addParameter(&custom_radio_station);
  wifiManager.addParameter(&custom_radio_volume);
  wifiManager.addParameter(&custom_clock_gmt_offset);
  wifiManager.addParameter(&custom_clock_daylight_offset);


  //set dark mode
  wifiManager.setDarkMode(true);

  //reset settings - for testing
  // wifiManager.resetSettings();

  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected to WiFi");
  Serial.print("Local ip: ");
  Serial.println(WiFi.localIP());

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());
  strcpy(radio_station, custom_radio_station.getValue());
  strcpy(radio_volume, custom_radio_volume.getValue());
  strcpy(clock_gmt_offset, custom_clock_gmt_offset.getValue());
  strcpy(clock_daylight_offset, custom_clock_daylight_offset.getValue());
  Serial.println("The values in the file are: ");
  Serial.println("\tmqtt_server : " + String(mqtt_server));
  Serial.println("\tmqtt_port : " + String(mqtt_port));
  Serial.println("\tmqtt_username : " + String(mqtt_username));
  Serial.println("\tmqtt_password : " + String(mqtt_password));
  Serial.println("\tmqtt_topic : " + String(mqtt_topic));
  Serial.println("\tradio_station : " + String(radio_station));
  Serial.println("\tradio_volume : " + String(radio_volume));
  Serial.println("\tclock_gmt_offset : " + String(clock_gmt_offset));
  Serial.println("\tclock_daylight_offset : " + String(clock_daylight_offset));

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    saveConfig();
  }

  // mqtt
  int mqttPort = atoi(mqtt_port);
  client.setServer(mqtt_server, mqttPort);
  client.setCallback(mqttCallback);

  // audio
  audio.setPinout(audioPinBClk, audioPinLRClk, audioPinData);
  audio.setVolume(atoi(radio_volume));
  audio.connecttohost(radio_station);
}

void loop() {
  //mqtt client check
  if (!client.connected()) reconnect();
  client.loop();


  audio.loop();


  // delay(LOOP_DELAY);
  vTaskDelay(1);
}