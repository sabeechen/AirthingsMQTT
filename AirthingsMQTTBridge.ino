/**
   A sketch that searches for a compatible Airthings device and 
   publishes the radon level, temperature, and humidity to an MQTT 
   server.

   The sketch was created with the intention to allow Airthings devices
   to cheaply integrate with Home Assistant.

   To use:
   (1) Set up your Airthings following the manufacter's instructions.
   (2) Install the PunSubClient library (https://pubsubclient.knolleary.net/).
   (3) Set your WiFi credentials below.
   (4) Set your MQTT server/credentials below.
   (5) Update the published topics below (if desired).
   (6) Flash to any ESP32 board.
   (7) Watch the Serial output to make sure it works.

   * The library runs once an hour to take a reading and deep sleeps in 
     between, so feasibly this could run on a battery for a very long time.
   * The library will attempt to find any airthings device to read from, 
     picking the first it finds.  The Airthings BLE API is unauthenticated 
     so no device configuration or pairing is necessary on the Airthings.
   * The library will not interfere with your Airthings' normal upload to a 
     phone/cloud.
   * If it fails to read, it will attempt again after 30 seconds instead.
   * I only have an Airthings Wave to test this with, though presumably it 
     would also work with the Wave Plus.
   * The ESP32's bluetooth stack is a little unstable IMHO so expect this to
     hang for a few minutes, restart prematurely, and report errors often.
*/
#include "BLEDevice.h"
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi credentials.
#define WIFI_SSID "YOUR SSID"
#define WIFI_PASS "YOUR PASSWORD"

// MQTT Settings.
#define MQTT_HOST "YOUR HOST"
#define MQTT_PORT 1883
#define MQTT_USER "YOUR USERNAME"
#define MQTT_PASS "YOUR PASSWORD"
#define MQTT_CLIENT "radon_client"

// The MQTT topic to publish a 24 hour average of radon levels to.
#define TOPIC_RADON_24HR "stat/airthings/radon24hour"
// The MQTT topic to publish the lifetime radon average to.  Documentation
// says this will be the average ever since the batteries were removed.
#define TOPIC_RADON_LIFETIME "stat/airthings/radonLifetime"
// Topics for temperature and humidity.
#define TOPIC_TEMPERATURE "stat/airthings/temperature"
#define TOPIC_HUMIDITY "stat/airthings/humidity"

// Unlikely you'll need to chnage any of the settings below.

// The time to take between readings.  One hour has worked pretty well for me.  
// Since the device only gives us the 24hr average, more frequent readings 
// probably wouldn't be useful, run the airthings battery down, and risk 
// interfering with the "normal" mechanism Airthings uses to publish info
// to your phone.
#define READ_WAIT_SECONDS 60*60 // One hour

// If taking a reading fails for any reason (BLE is pretty flaky...) then
// the ESP will sleep for this long before retrying.
#define READ_WAIT_RETRY_SECONDS 30

// How long the ESP will wait to connect to WiFi, scan for 
// Airthings devices, etc.
#define CONNECT_WAIT_SECONDS 30

// Some useful constants.
#define uS_TO_S_FACTOR 1000000
#define SECONDS_TO_MILLIS 1000
#define BECQUERELS_M2_TO_PICOCURIES_L 37.0
#define DOT_PRINT_INTERVAL 50

// The hard-coded uuid's airthings uses to advertise itself and its data.
static BLEUUID serviceUUID("b42e1f6e-ade7-11e4-89d3-123b93f75cba");
static BLEUUID charUUID("b42e01aa-ade7-11e4-89d3-123b93f75cba");
static BLEUUID radon24UUID("b42e01aa-ade7-11e4-89d3-123b93f75cba");
static BLEUUID radonLongTermUUID("b42e0a4c-ade7-11e4-89d3-123b93f75cba");
static BLEUUID datetimeUUID((uint32_t)0x2A08);
static BLEUUID temperatureUUID((uint32_t)0x2A6E);
static BLEUUID humidityUUID((uint32_t)0x2A6F);

bool getAndRecordReadings(BLEAddress pAddress) {
  Serial.println();
  Serial.println("Connecting...");
  BLEClient* client = BLEDevice::createClient();

  // Connect to the remove BLE Server.
  if (!client->connect(pAddress)) {
    Serial.println("Failed to connect.");
    return false;
  }

  Serial.println("Connected!");
  // Obtain a reference to the service we are after in the remote BLE server.
  Serial.println("Retrieving service reference...");
  BLERemoteService* pRemoteService = client->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Airthings refused its service UUID.");
    client->disconnect();
    return false;
  }

  // Get references to our characteristics
  Serial.println("Reading radon/temperature/humidity...");
  BLERemoteCharacteristic* temperatureCharacteristic = pRemoteService->getCharacteristic(temperatureUUID);
  BLERemoteCharacteristic* humidityCharacteristic = pRemoteService->getCharacteristic(humidityUUID);
  BLERemoteCharacteristic* radon24Characteristic = pRemoteService->getCharacteristic(radon24UUID);
  BLERemoteCharacteristic* radonLongTermCharacteristic = pRemoteService->getCharacteristic(radonLongTermUUID);
  
  if (temperatureCharacteristic == nullptr ||
      humidityCharacteristic == nullptr ||
      radon24Characteristic == nullptr || 
      radonLongTermCharacteristic == nullptr) {
    Serial.print("Failed to read from the device!");
    return false;
  }

  float temperature = ((short)temperatureCharacteristic->readUInt16()) / 100.0;
  float humidity = humidityCharacteristic->readUInt16() / 100.0;

  // The radon values are reported in terms of 
  float radon = radon24Characteristic->readUInt16() / BECQUERELS_M2_TO_PICOCURIES_L;
  float radonLongterm = radonLongTermCharacteristic->readUInt16() / BECQUERELS_M2_TO_PICOCURIES_L;
  client->disconnect();
  
  Serial.printf("Temperature: %f\n", temperature);
  Serial.printf("Humidity: %f\n", humidity);
  Serial.printf("Radon 24hr average: %f\n", radon);
  Serial.printf("Radon Lifetime average: %f\n", radonLongterm);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() < start + CONNECT_WAIT_SECONDS * SECONDS_TO_MILLIS) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to wifi");
    return false;
  }

  
  // Connect and publish to MQTT.
  WiFiClient espClient;
  PubSubClient mqtt(espClient);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  if (!mqtt.connect("RADON_CLIENT", MQTT_USER, MQTT_PASS) ||
      !mqtt.publish(TOPIC_RADON_24HR, String(radon).c_str()) ||
      !mqtt.publish(TOPIC_RADON_LIFETIME, String(radonLongterm).c_str()) ||
      !mqtt.publish(TOPIC_TEMPERATURE, String(temperature).c_str()) ||
      !mqtt.publish(TOPIC_HUMIDITY, String(humidity).c_str())) {
    Serial.println("Unable to connect/publish to mqtt server.");
    return false;
  }
  return true;
}

// The bluetooth stack takes a callback when scannign for devices.  The first Airthings device it finds it will record in pServerAddress.
class FoundDeviceCallback: public BLEAdvertisedDeviceCallbacks {
  public: 
  BLEAddress* address;
  bool found = false;
  bool foundAirthings() {
    return found;
  }
  BLEAddress getAddress() {
    return *address;
  }
  void onResult(BLEAdvertisedDevice device) {
    // We have found a device, see if it has the Airthings service UUID
    if (device.haveServiceUUID() && device.getServiceUUID().equals(serviceUUID)) {
      Serial.print("Found our device: ");
      Serial.println(device.toString().c_str());
      device.getScan()->stop();
      address = new BLEAddress(device.getAddress());
      found = true;
    }
  }
};

void setup() {
  Serial.begin(115200);

  // Start up WiFi early so it'll probably be ready by the time 
  // we're reading from Airthings.
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Scan for an Airthings device.
  Serial.println("Scanning for airthings devices");
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  FoundDeviceCallback* callback = new FoundDeviceCallback();
  pBLEScan->setAdvertisedDeviceCallbacks(callback);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(30);

  unsigned long timeToSleep = 0;
  if (!callback->foundAirthings()) {
    // We timed out looking for an Airthings.
    Serial.printf("\nFAILED to find any Airthings devices. Sleeping for %i seconds before retrying.\n", READ_WAIT_RETRY_SECONDS);
    timeToSleep = READ_WAIT_RETRY_SECONDS;
  } else if (getAndRecordReadings(callback->getAddress())) {
    Serial.printf("\nReading complete. Sleeping for %i seconds before taking another reading.\n", READ_WAIT_SECONDS);
    timeToSleep = READ_WAIT_SECONDS;
  } else {
    Serial.printf("\nReading FAILED. Sleeping for %i seconds before retrying.\n", READ_WAIT_RETRY_SECONDS);
    timeToSleep = READ_WAIT_RETRY_SECONDS;
  }
  Serial.flush();
  esp_sleep_enable_timer_wakeup(timeToSleep * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {
  // We should never reach here.
  delay(1);
}
