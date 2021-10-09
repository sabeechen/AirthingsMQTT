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
#define MQTT_CLIENT "airthings"

// The MQTT topic to publish a 24 hour average of radon levels to.
#define TOPIC_RADON_24HR "airthings/radon24hour"
// The MQTT topic to publish the lifetime radon average to.  Documentation
// says this will be the average ever since the batteries were removed.
#define TOPIC_RADON_LIFETIME "airthings/radonLifetime"
// Topics for temperature and humidity.
#define TOPIC_TEMPERATURE "airthings/temperature"
#define TOPIC_HUMIDITY "airthings/humidity"
#define TOPIC_PRESSURE "airthings/pressure"
#define TOPIC_CO2 "airthings/co2"
#define TOPIC_VOC "airthings/voc"
#define TOPIC_VERSION "airthings/version"
#define TOPIC_AMBIENTLIGHT "airthings/ambientlight"

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

#define ONBOARD_LED 2

// The hard-coded uuid's airthings uses to advertise itself and its data.
static BLEUUID waveServiceUUID("b42e1f6e-ade7-11e4-89d3-123b93f75cba"); // wave
static BLEUUID charUUID("b42e01aa-ade7-11e4-89d3-123b93f75cba");
static BLEUUID radon24UUID("b42e01aa-ade7-11e4-89d3-123b93f75cba");
static BLEUUID radonLongTermUUID("b42e0a4c-ade7-11e4-89d3-123b93f75cba");
static BLEUUID datetimeUUID((uint32_t)0x2A08);
static BLEUUID temperatureUUID((uint32_t)0x2A6E);
static BLEUUID humidityUUID((uint32_t)0x2A6F);

// waveplus
static BLEUUID wavePlusServiceUUID("b42e1c08-ade7-11e4-89d3-123b93f75cba");
static BLEUUID characteristicUUID("b42e2a68-ade7-11e4-89d3-123b93f75cba");

typedef enum
{
  WAVE,
  WAVEPLUS
} AirthingsProduct;

typedef struct {
  float temperature;
  float humidity;
  float radon;
  float radonLongterm;
} WaveRawReadings;

typedef struct
{
  uint8_t version = 0;
  uint8_t humidity = 0;
  uint8_t ambientLight = 0;
  uint8_t unused01 = 0;
  uint16_t radon = 0;
  uint16_t radonLongterm = 0;
  uint16_t temperature = 0;
  uint16_t pressure = 0;
  uint16_t co2 = 0;
  uint16_t voc = 0;
} WavePlusRawReadings;

typedef struct
{
  String version;
  String humidity;
  String ambientLight;
  String radon;
  String radonLongterm;
  String temperature;
  String pressure;
  String co2;
  String voc;
} WaveReadings;

bool readSensors(AirthingsProduct airthingsProduct, BLERemoteService *pRemoteService, WaveReadings *readings)
{
  bool retval = false;
  switch (airthingsProduct)
  {
    case WAVE:
      retval = readWaveSensors(pRemoteService, readings);
    case WAVEPLUS:
      retval = readWavePlusSensors(pRemoteService, readings);
  }
    return retval;
}

bool readWaveSensors(BLERemoteService *pRemoteService, WaveReadings *readings)
{
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

  readings->temperature = String(((short)temperatureCharacteristic->readUInt16()) / 100.0, 2);
  readings->humidity = String(humidityCharacteristic->readUInt16() / 100.0, 2);

  // The radon values are reported in terms of
  readings->radon = String(radon24Characteristic->readUInt16() / BECQUERELS_M2_TO_PICOCURIES_L);
  readings->radonLongterm = String(radonLongTermCharacteristic->readUInt16() / BECQUERELS_M2_TO_PICOCURIES_L);

  return true;
}

bool readWavePlusSensors(BLERemoteService *pRemoteService, WaveReadings *readings)
{
  BLERemoteCharacteristic *pCharacteristic = pRemoteService->getCharacteristic(characteristicUUID);
  pCharacteristic->readValue();
  uint8_t *pRawdata = pCharacteristic->readRawData();
  if (pRawdata == nullptr) {
    Serial.println("Error, null characteristics");
    return false;
  }

  WavePlusRawReadings *rawReadings = (WavePlusRawReadings *)pRawdata;
  readings->version = String(rawReadings->version);
  readings->humidity = String(rawReadings->humidity / 2.0f, 2);
  readings->ambientLight = String(rawReadings->ambientLight);
  readings->radon = String(rawReadings->radon);
  readings->radonLongterm = String(rawReadings->radonLongterm);
  readings->temperature = String(rawReadings->temperature / 100.0f, 2);
  readings->pressure = String(rawReadings->pressure / 50.0f, 1);
  readings->co2 = String(rawReadings->co2);
  readings->voc = String(rawReadings->voc);

  return true;
}

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
  AirthingsProduct airthingsProduct = WAVE;
  BLERemoteService *pRemoteService = client->getService(waveServiceUUID);
  if (pRemoteService == nullptr)
  {
    Serial.println("No Wave, trying Wave Plus..");
    pRemoteService = client->getService(wavePlusServiceUUID);
    airthingsProduct = WAVEPLUS;
  }

  if (pRemoteService == nullptr) {
    Serial.println("Airthings refused its service UUID.");
    client->disconnect();
    return false;
  }

  WaveReadings readings;
  if (!readSensors(airthingsProduct, pRemoteService, &readings))
  {
    return false;
  }

  client->disconnect();

  Serial.printf("Radon 24hr average: %s\n", readings.radon.c_str());
  Serial.printf("Radon Lifetime average: %s\n", readings.radonLongterm.c_str());
  Serial.printf("Humidity: %s\n", readings.humidity.c_str());
  Serial.printf("Temperature: %s\n", readings.temperature);
  if (airthingsProduct == WAVEPLUS)
  {
    Serial.printf("Version: %s\n", readings.version.c_str());
    Serial.printf("Ambient light: %s\n", readings.ambientLight.c_str());
    Serial.printf("Pressure: %s\n", readings.pressure.c_str());
    Serial.printf("CO2: %s\n", readings.co2.c_str());
    Serial.printf("VOC: %s\n", readings.voc.c_str());
  }

  Serial.println("Checking wifi connection..");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() < start + CONNECT_WAIT_SECONDS * SECONDS_TO_MILLIS) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to wifi");
    return false;
  }

  Serial.println("Wifi connection alive");

  // Connect and publish to MQTT.
  WiFiClient espClient;
  PubSubClient mqtt(espClient);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  // TODO: if WAVEPLUS, also publish CO2 and VOC
  Serial.println("Connecting to MQTT server..");
  if (!mqtt.connect(MQTT_CLIENT, MQTT_USER, MQTT_PASS)) {
    Serial.println(".. error connecting!");
    return false;
  }
  Serial.println(".. connected.");

  Serial.println("Publishing to MQTT..");
  if (!mqtt.publish(TOPIC_RADON_24HR, readings.radon.c_str()) ||
      !mqtt.publish(TOPIC_RADON_LIFETIME, readings.radonLongterm.c_str()) ||
      !mqtt.publish(TOPIC_TEMPERATURE, readings.temperature.c_str()) ||
      !mqtt.publish(TOPIC_HUMIDITY, readings.humidity.c_str()))
  {
    Serial.println(".. error publishing!");
    return false;
  }

  if (airthingsProduct == WAVEPLUS)
  {
    if (!mqtt.publish(TOPIC_PRESSURE, readings.pressure.c_str()) ||
        !mqtt.publish(TOPIC_CO2, readings.co2.c_str()) ||
        !mqtt.publish(TOPIC_VOC, readings.voc.c_str()) ||
        !mqtt.publish(TOPIC_VERSION, readings.version.c_str()) ||
        !mqtt.publish(TOPIC_AMBIENTLIGHT, readings.ambientLight.c_str()))
    {
      Serial.println(".. error publishing!");
      return false;
    }
  }

  Serial.println(".. published successfully.");

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
    if (device.haveServiceUUID() && device.getServiceUUID().equals(waveServiceUUID)
      || device.haveServiceUUID() && device.getServiceUUID().equals(wavePlusServiceUUID)) {
      Serial.print("Found device: ");
      Serial.println(device.toString().c_str());
      device.getScan()->stop();
      address = new BLEAddress(device.getAddress());
      found = true;
    }
  }
};

void setup() {
  Serial.begin(115200);
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, HIGH);

  // Start up WiFi early so it'll probably be ready by the time 
  // we're reading from Airthings.
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Scan for an Airthings device.
  Serial.println("Scanning for devices");
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  FoundDeviceCallback* callback = new FoundDeviceCallback();
  pBLEScan->setAdvertisedDeviceCallbacks(callback);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(30);

  delay(500); // give callback some time to execute

  unsigned long timeToSleep = 0;
  if (!callback->foundAirthings()) {
    // We timed out looking for an Airthings.
    Serial.printf("\nFAILED to find devices. Sleeping %i seconds..\n", READ_WAIT_RETRY_SECONDS);
    timeToSleep = READ_WAIT_RETRY_SECONDS;
  } else if (getAndRecordReadings(callback->getAddress())) {
    Serial.printf("\nReading complete. Sleeping %i seconds..\n", READ_WAIT_SECONDS);
    timeToSleep = READ_WAIT_SECONDS;
  } else {
    Serial.printf("\nReading FAILED. Sleeping %i seconds..\n", READ_WAIT_RETRY_SECONDS);
    timeToSleep = READ_WAIT_RETRY_SECONDS;
  }
  Serial.flush();
  digitalWrite(ONBOARD_LED, LOW);
  esp_sleep_enable_timer_wakeup(timeToSleep * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {
  // We should never reach here.
  delay(1);
}
