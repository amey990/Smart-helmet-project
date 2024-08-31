/*#include <HTTP_Method.h> // Normal without active inactive status
#include <Uri.h>
#include <WebServer.h>
#include <Wire.h>
#include <WiFi.h>
#include <SPI.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "certss.h"
#include <Adafruit_Sensor.h>
#include "Adafruit_MLX90614.h"
//#include "MAX30105.h" // Commenting out the MAX30105 header
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Multichannel_Gas_GMXXX.h>

#define RXD2 16 // Define RXD2 pin
#define TXD2 17 // Define TXD2 pin
#define ID "01"
#define LED_PIN 2 // LED pin definition

// Define I2C addresses for each device
#define GAS_SENSOR_ADDRESS 0x08
#define MLX_SENSOR_ADDRESS 0x5A
//#define MAX_SENSOR_ADDRESS 0x57 // Commenting out the address for MAX30105

// MAX30105 particleSensor; // Commenting out the initialization of MAX30105
GAS_GMXXX<TwoWire> gasSensor;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
#define DHTPIN 1
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

unsigned long ledStartTime = 0;
#define THINGNAME "Smart_Helmet"
const char ssid[] = "me";
const char password[] = "12345678";
const char AWS_IOT_ENDPOINT[] ="a27mqcf3cydka7-ats.iot.us-east-1.amazonaws.com";

#define AWS_IOT_PUBLISH_TOPIC "smart01/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "smart01/sub"

WiFiClientSecure wifi_client;
MQTTClient mqtt_client(256);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000); // Offset for IST
uint32_t t1;


float calculateHeartRate(uint32_t irValue) {
    return irValue / 1000.0;
}


bool checkSensors() {
    bool allSensorsWorking = true;

    // DHT22 Sensor Check
    if (isnan(dht.readTemperature())) {
        Serial.println("DHT22 Sensor Error");
        allSensorsWorking = false;
    }

    
    // MAX30105 Sensor Check (Heart Rate)
    uint32_t irValue = particleSensor.getIR();
    if (irValue < 50000) {
        Serial.println("MAX30105 Sensor Error");
        allSensorsWorking = false;
    } else if (irValue == 0) {
        // Default value should be 1 if the sensor is active
        irValue = 1;
    }
    

    // MLX90614 Sensor Check
    if (isnan(mlx.readObjectTempC())) {
        Serial.println("MLX90614 Sensor Error");
        allSensorsWorking = false;
    }

    // Multichannel Gas Sensor Check
    if (gasSensor.getGM502B() == 0 && gasSensor.getGM702B() == 0 && gasSensor.getGM102B() == 0 && gasSensor.getGM302B() == 0) {
        Serial.println("Multichannel Gas Sensor Error");
        allSensorsWorking = false;
    }

    return allSensorsWorking;
}

void connectAWS() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("Connecting to Wi-Fi");
    int wifi_attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        wifi_attempts++;
        if (wifi_attempts > 60) {
            Serial.println("Failed to connect to Wi-Fi");
            return;
        }
    }
    Serial.println();
    Serial.print("Connected to Wi-Fi. IP address: ");
    Serial.println(WiFi.localIP());

    wifi_client.setCACert(AWS_CERT_CA);
    wifi_client.setCertificate(AWS_CERT_CRT);
    wifi_client.setPrivateKey(AWS_CERT_PRIVATE);

    mqtt_client.begin(AWS_IOT_ENDPOINT, 8883, wifi_client);
    mqtt_client.onMessage(incomingMessageHandler);

    Serial.print("Connecting to AWS IOT");
    while (!mqtt_client.connect(THINGNAME)) {
        Serial.print(".");
        delay(100);
    }
    Serial.println();
    if (!mqtt_client.connected()) {
        Serial.println("AWS IoT Timeout!");
        return;
    }
    mqtt_client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
    Serial.println("AWS IoT Connected!");

    timeClient.begin();
    timeClient.update();
    setTime(timeClient.getEpochTime());
}

void publishMessage() {
    DynamicJsonDocument doc(512);

    int objTemp = mlx.readObjectTempC();
    // uint32_t irValue = particleSensor.getIR(); // Commenting out IR value reading
    // int heartRate = calculateHeartRate(irValue); // Commenting out heart rate calculation
    unsigned int voc = gasSensor.getGM502B();
    unsigned int co = gasSensor.getGM702B();
    unsigned int no2 = gasSensor.getGM102B();
    unsigned int c2h5ch = gasSensor.getGM302B();
    int t = dht.readTemperature(); 

    doc["Env_temp"] = t;
    doc["Obj_temp"] = isnan(objTemp) ? 0 : objTemp;
    // doc["Hrt"] = heartRate; // Commenting out heart rate data
    doc["VOLATILE_GAS"] = voc;
    doc["CARBON_MONOXIDE"] = co;
    doc["NITROGEN_DIOXIDE"] = no2;
    doc["ALCOHOL"] = c2h5ch;
    doc["time"] = millis() - t1;
    doc["Device_ID"] = ID;

    char dateTime[20];
    sprintf(dateTime, "%04d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
    doc["datetime"] = dateTime;

    // Determine Helmet Status
    if (checkSensors()) {
        doc["Helmet_Status"] = "Active";
        Serial.println("Helmet is Active");
    } else {
        doc["Helmet_Status"] = "Inactive";
        Serial.println("Helmet is Inactive");
    }

    String jsonString;
    serializeJson(doc, jsonString);
    mqtt_client.publish(AWS_IOT_PUBLISH_TOPIC, jsonString.c_str());
    Serial.println("Sent a message: " + jsonString);
}

void incomingMessageHandler(String &topic, String &payload) {
    Serial.println("Message received!");
    Serial.println("Topic: " + topic);
    Serial.println("Payload: " + payload);

    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);

    if (doc.containsKey("LED")) {
        bool ledState = doc["LED"];
        
        if (ledState) {
            digitalWrite(LED_PIN, HIGH);
            ledStartTime = millis();
            Serial.println("LED is now ON");
        } else {
            digitalWrite(LED_PIN, LOW);
            Serial.println("LED is now OFF");
        }
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    t1 = millis();
    dht.begin();
    Serial.println("Setup started");

    Wire.begin();

    gasSensor.begin(Wire, GAS_SENSOR_ADDRESS);
    mlx.begin(MLX_SENSOR_ADDRESS);
    // particleSensor.begin(Wire); // Commenting out particle sensor initialization
    // particleSensor.setup(); // Commenting out particle sensor setup
    // particleSensor.setPulseAmplitudeRed(0x0A); // Commenting out pulse amplitude setting

    Serial.println("Setup complete");

    connectAWS();
}

void loop() {
    if (digitalRead(LED_PIN) == HIGH && millis() - ledStartTime > 5000) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED turned OFF automatically after 5 seconds");
    }

    publishMessage();
    mqtt_client.loop();
    delay(4000);
}











/*

#include <HTTP_Method.h>
#include <Uri.h>
#include <WebServer.h>
#include <Wire.h>
#include <WiFi.h>
#include <SPI.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "certss.h"
#include <Adafruit_Sensor.h>
#include "Adafruit_MLX90614.h"
#include "MAX30105.h" // Uncommenting the MAX30105 header
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Multichannel_Gas_GMXXX.h>

#define RXD2 16
#define TXD2 17
#define ID "01"
#define LED_PIN 2

#define GAS_SENSOR_ADDRESS 0x08
#define MLX_SENSOR_ADDRESS 0x5A
#define MAX_SENSOR_ADDRESS 0x57 // Uncommenting the address for MAX30105

MAX30105 particleSensor; // Uncommenting the initialization of MAX30105
GAS_GMXXX<TwoWire> gasSensor;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
#define DHTPIN 1
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

unsigned long ledStartTime = 0;
#define THINGNAME "Smart_Helmet"
const char ssid[] = "me";
const char password[] = "12345678";
const char AWS_IOT_ENDPOINT[] ="a27mqcf3cydka7-ats.iot.us-east-1.amazonaws.com";

#define AWS_IOT_PUBLISH_TOPIC "smart01/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "smart01/sub"

WiFiClientSecure wifi_client;
MQTTClient mqtt_client(256);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);
uint32_t t1;

float calculateHeartRate(uint32_t irValue) {
    if (irValue < 50000) {
        return 68; // Default heart rate value when the sensor is inactive
    }
    return irValue / 1000.0;
}

bool checkSensors() {
    bool allSensorsWorking = true;

    // DHT22 Sensor Check
    if (isnan(dht.readTemperature())) {
        Serial.println("DHT22 Sensor Error");
        allSensorsWorking = false;
    }

    // MAX30105 Sensor Check (Heart Rate)
    uint32_t irValue = particleSensor.getIR();
    if (irValue < 50000) {
        Serial.println("MAX30105 Sensor Error");
        allSensorsWorking = false;
    }

    // MLX90614 Sensor Check
    if (isnan(mlx.readObjectTempC())) {
        Serial.println("MLX90614 Sensor Error");
        allSensorsWorking = false;
    }

    // Multichannel Gas Sensor Check
    if (gasSensor.getGM502B() == 0 && gasSensor.getGM702B() == 0 && gasSensor.getGM102B() == 0 && gasSensor.getGM302B() == 0) {
        Serial.println("Multichannel Gas Sensor Error");
        allSensorsWorking = false;
    }

    return allSensorsWorking;
}

void connectAWS() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("Connecting to Wi-Fi");
    int wifi_attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        wifi_attempts++;
        if (wifi_attempts > 60) {
            Serial.println("Failed to connect to Wi-Fi");
            return;
        }
    }
    Serial.println();
    Serial.print("Connected to Wi-Fi. IP address: ");
    Serial.println(WiFi.localIP());

    wifi_client.setCACert(AWS_CERT_CA);
    wifi_client.setCertificate(AWS_CERT_CRT);
    wifi_client.setPrivateKey(AWS_CERT_PRIVATE);

    mqtt_client.begin(AWS_IOT_ENDPOINT, 8883, wifi_client);
    mqtt_client.onMessage(incomingMessageHandler);

    Serial.print("Connecting to AWS IOT");
    while (!mqtt_client.connect(THINGNAME)) {
        Serial.print(".");
        delay(100);
    }
    Serial.println();
    if (!mqtt_client.connected()) {
        Serial.println("AWS IoT Timeout!");
        return;
    }
    mqtt_client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
    Serial.println("AWS IoT Connected!");

    timeClient.begin();
    timeClient.update();
    setTime(timeClient.getEpochTime());
}

void publishMessage() {
    DynamicJsonDocument doc(512);

    int objTemp = mlx.readObjectTempC();
    uint32_t irValue = particleSensor.getIR(); 
    int heartRate = calculateHeartRate(irValue);
    unsigned int voc = gasSensor.getGM502B();
    unsigned int co = gasSensor.getGM702B();
    unsigned int no2 = gasSensor.getGM102B();
    unsigned int c2h5ch = gasSensor.getGM302B();
    int t = dht.readTemperature(); 

    doc["Env_temp"] = t;
    doc["Obj_temp"] = isnan(objTemp) ? 0 : objTemp;
    doc["Hrt"] = heartRate; // Including heart rate data
    doc["VOLATILE_GAS"] = voc;
    doc["CARBON_MONOXIDE"] = co;
    doc["NITROGEN_DIOXIDE"] = no2;
    doc["ALCOHOL"] = c2h5ch;
    doc["time"] = millis() - t1;
    doc["Device_ID"] = ID;

    char dateTime[20];
    sprintf(dateTime, "%04d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
    doc["datetime"] = dateTime;

    if (checkSensors()) {
        doc["Helmet_Status"] = "Active";
        Serial.println("Helmet is Active");
    } else {
        doc["Helmet_Status"] = "Inactive";
        Serial.println("Helmet is Active");
    }

    String jsonString;
    serializeJson(doc, jsonString);
    mqtt_client.publish(AWS_IOT_PUBLISH_TOPIC, jsonString.c_str());
    Serial.println("Sent a message: " + jsonString);
}

void incomingMessageHandler(String &topic, String &payload) {
    Serial.println("Message received!");
    Serial.println("Topic: " + topic);
    Serial.println("Payload: " + payload);

    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);

    if (doc.containsKey("LED")) {
        bool ledState = doc["LED"];
        
        if (ledState) {
            digitalWrite(LED_PIN, HIGH);
            ledStartTime = millis();
            Serial.println("LED is now ON");
        } else {
            digitalWrite(LED_PIN, LOW);
            Serial.println("LED is now OFF");
        }
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    t1 = millis();
    dht.begin();
    Serial.println("Setup started");

    Wire.begin();

    gasSensor.begin(Wire, GAS_SENSOR_ADDRESS);
    mlx.begin(MLX_SENSOR_ADDRESS);
    particleSensor.begin(Wire); // Uncommenting particle sensor initialization
    particleSensor.setup(); // Uncommenting particle sensor setup
    particleSensor.setPulseAmplitudeRed(0x0A); // Uncommenting pulse amplitude setting

    Serial.println("Setup complete");

    connectAWS();
}

void loop() {
    if (digitalRead(LED_PIN) == HIGH && millis() - ledStartTime > 5000) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED turned OFF automatically after 5 seconds");
    }

    publishMessage();
    mqtt_client.loop();
    delay(4000);
}












#include <HTTP_Method.h>
#include <Uri.h>
#include <WebServer.h>
#include <Wire.h>
#include <WiFi.h>
#include <SPI.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "certss.h"
#include <Adafruit_Sensor.h>
#include "Adafruit_MLX90614.h"
#include "MAX30105.h" // Uncommenting the MAX30105 header
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Multichannel_Gas_GMXXX.h>

#define RXD2 16
#define TXD2 17
#define ID "01"
#define LED_PIN 2

#define GAS_SENSOR_ADDRESS 0x08
#define MLX_SENSOR_ADDRESS 0x5A
#define MAX_SENSOR_ADDRESS 0x57 // Uncommenting the address for MAX30105

MAX30105 particleSensor; // Uncommenting the initialization of MAX30105
GAS_GMXXX<TwoWire> gasSensor;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
#define DHTPIN 1
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

unsigned long ledStartTime = 0;
#define THINGNAME "Smart_Helmet"
const char ssid[] = "me";
const char password[] = "12345678";
const char AWS_IOT_ENDPOINT[] ="a27mqcf3cydka7-ats.iot.us-east-1.amazonaws.com";

#define AWS_IOT_PUBLISH_TOPIC "smart01/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "smart01/sub"

WiFiClientSecure wifi_client;
MQTTClient mqtt_client(256);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);
uint32_t t1;

float calculateHeartRate(uint32_t irValue) {
    if (irValue < 50000) {
        return 68; // Default heart rate value when the sensor is inactive
    }
    return irValue / 1000.0;
}

bool checkSensors() {
    bool allSensorsWorking = true;

    // DHT22 Sensor Check
    if (isnan(dht.readTemperature())) {
        Serial.println("DHT22 Sensor Error");
        allSensorsWorking = false;
    }

    // MAX30105 Sensor Check (Heart Rate)
    uint32_t irValue = particleSensor.getIR();
    if (irValue < 50000) {
        Serial.println("MAX30105 Sensor Error");
        allSensorsWorking = false;
    }

    // MLX90614 Sensor Check
    if (isnan(mlx.readObjectTempC())) {
        Serial.println("MLX90614 Sensor Error");
        allSensorsWorking = false;
    }

    // Multichannel Gas Sensor Check
    if (gasSensor.getGM502B() == 0 && gasSensor.getGM702B() == 0 && gasSensor.getGM102B() == 0 && gasSensor.getGM302B() == 0) {
        Serial.println("Multichannel Gas Sensor Error");
        allSensorsWorking = false;
    }

    return allSensorsWorking;
}

void connectAWS() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("Connecting to Wi-Fi");
    int wifi_attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        wifi_attempts++;
        if (wifi_attempts > 60) {
            Serial.println("Failed to connect to Wi-Fi");
            return;
        }
    }
    Serial.println();
    Serial.print("Connected to Wi-Fi. IP address: ");
    Serial.println(WiFi.localIP());

    wifi_client.setCACert(AWS_CERT_CA);
    wifi_client.setCertificate(AWS_CERT_CRT);
    wifi_client.setPrivateKey(AWS_CERT_PRIVATE);

    mqtt_client.begin(AWS_IOT_ENDPOINT, 8883, wifi_client);
    mqtt_client.onMessage(incomingMessageHandler);

    Serial.print("Connecting to AWS IOT");
    while (!mqtt_client.connect(THINGNAME)) {
        Serial.print(".");
        delay(100);
    }
    Serial.println();
    if (!mqtt_client.connected()) {
        Serial.println("AWS IoT Timeout!");
        return;
    }
    mqtt_client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
    Serial.println("AWS IoT Connected!");

    timeClient.begin();
    timeClient.update();
    setTime(timeClient.getEpochTime());
}

void publishMessage() {
    DynamicJsonDocument doc(512);

    int objTemp = mlx.readObjectTempC();
    uint32_t irValue = particleSensor.getIR(); 
    int heartRate = calculateHeartRate(irValue);
    unsigned int voc = gasSensor.getGM502B();
    unsigned int co = gasSensor.getGM702B();
    unsigned int no2 = gasSensor.getGM102B();
    unsigned int c2h5ch = gasSensor.getGM302B();
    int t = dht.readTemperature(); 

    doc["Env_temp"] = t;
    doc["Obj_temp"] = isnan(objTemp) ? 0 : objTemp;
    doc["Hrt"] = heartRate; // Including heart rate data
    doc["VOLATILE_GAS"] = voc;
    doc["CARBON_MONOXIDE"] = co;
    doc["NITROGEN_DIOXIDE"] = no2;
    doc["ALCOHOL"] = c2h5ch;
    doc["time"] = millis() - t1;
    doc["Device_ID"] = ID;

    char dateTime[20];
    sprintf(dateTime, "%04d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
    doc["datetime"] = dateTime;

    if (checkSensors()) {
        doc["Helmet_Status"] = "Active";
        Serial.println("Helmet is Active");
    } else {
        doc["Helmet_Status"] = "Inactive";
        Serial.println("Helmet is Inactive");
    }

    String jsonString;
    serializeJson(doc, jsonString);
    mqtt_client.publish(AWS_IOT_PUBLISH_TOPIC, jsonString.c_str());
    Serial.println("Sent a message: " + jsonString);
}

void incomingMessageHandler(String &topic, String &payload) {
    Serial.println("Message received!");
    Serial.println("Topic: " + topic);
    Serial.println("Payload: " + payload);

    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);

    if (doc.containsKey("LED")) {
        bool ledState = doc["LED"];
        
        if (ledState) {
            digitalWrite(LED_PIN, HIGH);
            ledStartTime = millis();
            Serial.println("LED is now ON");
        } else {
            digitalWrite(LED_PIN, LOW);
            Serial.println("LED is now OFF");
        }
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    t1 = millis();
    dht.begin();
    Serial.println("Setup started");

    Wire.begin();

    gasSensor.begin(Wire, GAS_SENSOR_ADDRESS);
    mlx.begin(MLX_SENSOR_ADDRESS);
    particleSensor.begin(Wire); // Uncommenting particle sensor initialization
    particleSensor.setup(); // Uncommenting particle sensor setup
    particleSensor.setPulseAmplitudeRed(0x0A); // Uncommenting pulse amplitude setting

    Serial.println("Setup complete");

    connectAWS();
}

void loop() {
    if (digitalRead(LED_PIN) == HIGH && millis() - ledStartTime > 5000) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED turned OFF automatically after 5 seconds");
    }

    publishMessage();
    mqtt_client.loop();
    delay(4000);
}


#include <HTTP_Method.h>
#include <Uri.h>
#include <WebServer.h>
#include <Wire.h>
#include <WiFi.h>
#include <SPI.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "certss.h"
#include <Adafruit_Sensor.h>
#include "Adafruit_MLX90614.h"
#include "MAX30105.h" // Uncommenting the MAX30105 header
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Multichannel_Gas_GMXXX.h>

#define RXD2 16
#define TXD2 17
#define ID "01"
#define LED_PIN 2

#define GAS_SENSOR_ADDRESS 0x08
#define MLX_SENSOR_ADDRESS 0x5A
#define MAX_SENSOR_ADDRESS 0x57 // Uncommenting the address for MAX30105

MAX30105 particleSensor; // Uncommenting the initialization of MAX30105
GAS_GMXXX<TwoWire> gasSensor;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
#define DHTPIN 1
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

unsigned long ledStartTime = 0;
#define THINGNAME "Smart_Helmet"
const char ssid[] = "me";
const char password[] = "12345678";
const char AWS_IOT_ENDPOINT[] ="a27mqcf3cydka7-ats.iot.us-east-1.amazonaws.com";

#define AWS_IOT_PUBLISH_TOPIC "smart01/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "smart01/sub"

WiFiClientSecure wifi_client;
MQTTClient mqtt_client(256);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);
uint32_t t1;

float calculateHeartRate(uint32_t irValue) {
    if (irValue < 50000) {
        return 1; // Default heart rate value when the sensor is inactive
    }
    return irValue / 1000.0;
}

bool checkSensors() {
    bool allSensorsWorking = true;

    // DHT22 Sensor Check
    if (isnan(dht.readTemperature())) {
        Serial.println("DHT22 Sensor Error");
        allSensorsWorking = false;
    }

    // MAX30105 Sensor Check (Heart Rate)
    uint32_t irValue = particleSensor.getIR();
    if (irValue < 50000) {
        Serial.println("MAX30105 Sensor Error");
        allSensorsWorking = false;
    }

    // MLX90614 Sensor Check
    if (isnan(mlx.readObjectTempC())) {
        Serial.println("MLX90614 Sensor Error");
        allSensorsWorking = false;
    }

    // Multichannel Gas Sensor Check
    if (gasSensor.getGM502B() == 0 && gasSensor.getGM702B() == 0 && gasSensor.getGM102B() == 0 && gasSensor.getGM302B() == 0) {
        Serial.println("Multichannel Gas Sensor Error");
        allSensorsWorking = false;
    }

    return allSensorsWorking;
}

void connectAWS() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("Connecting to Wi-Fi");
    int wifi_attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        wifi_attempts++;
        if (wifi_attempts > 60) {
            Serial.println("Failed to connect to Wi-Fi");
            return;
        }
    }
    Serial.println();
    Serial.print("Connected to Wi-Fi. IP address: ");
    Serial.println(WiFi.localIP());

    wifi_client.setCACert(AWS_CERT_CA);
    wifi_client.setCertificate(AWS_CERT_CRT);
    wifi_client.setPrivateKey(AWS_CERT_PRIVATE);

    mqtt_client.begin(AWS_IOT_ENDPOINT, 8883, wifi_client);
    mqtt_client.onMessage(incomingMessageHandler);

    Serial.print("Connecting to AWS IOT");
    while (!mqtt_client.connect(THINGNAME)) {
        Serial.print(".");
        delay(100);
    }
    Serial.println();
    if (!mqtt_client.connected()) {
        Serial.println("AWS IoT Timeout!");
        return;
    }
    mqtt_client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
    Serial.println("AWS IoT Connected!");

    timeClient.begin();
    timeClient.update();
    setTime(timeClient.getEpochTime());
}

void publishMessage() {
    DynamicJsonDocument doc(512);

    int objTemp = mlx.readObjectTempC();
    uint32_t irValue = particleSensor.getIR(); 
    int heartRate = calculateHeartRate(irValue);
    unsigned int voc = gasSensor.getGM502B();
    unsigned int co = gasSensor.getGM702B();
    unsigned int no2 = gasSensor.getGM102B();
    unsigned int c2h5ch = gasSensor.getGM302B();
    int t = dht.readTemperature(); 

    doc["Env_temp"] = t;
    doc["Obj_temp"] = isnan(objTemp) ? 0 : objTemp;
    doc["Hrt"] = heartRate; // Including heart rate data
    doc["VOLATILE_GAS"] = voc;
    doc["CARBON_MONOXIDE"] = co;
    doc["NITROGEN_DIOXIDE"] = no2;
    doc["ALCOHOL"] = c2h5ch;
    doc["time"] = millis() - t1;
    doc["Device_ID"] = ID;

    char dateTime[20];
    sprintf(dateTime, "%04d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
    doc["datetime"] = dateTime;

    if (checkSensors()) {
        doc["Helmet_Status"] = "Active";
        Serial.println("Helmet is Active");
    } else {
        doc["Helmet_Status"] = "Inactive";
        Serial.println("Helmet is Inactive");
    }

    String jsonString;
    serializeJson(doc, jsonString);
    mqtt_client.publish(AWS_IOT_PUBLISH_TOPIC, jsonString.c_str());
    Serial.println("Sent a message: " + jsonString);
}

void incomingMessageHandler(String &topic, String &payload) {
    Serial.println("Message received!");
    Serial.println("Topic: " + topic);
    Serial.println("Payload: " + payload);

    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);

    if (doc.containsKey("LED")) {
        bool ledState = doc["LED"];
        
        if (ledState) {
            digitalWrite(LED_PIN, HIGH);
            ledStartTime = millis();
            Serial.println("LED is now ON");
        } else {
            digitalWrite(LED_PIN, LOW);
            Serial.println("LED is now OFF");
        }
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    t1 = millis();
    dht.begin();
    Serial.println("Setup started");

    Wire.begin();

    gasSensor.begin(Wire, GAS_SENSOR_ADDRESS);
    mlx.begin(MLX_SENSOR_ADDRESS);
    particleSensor.begin(Wire); // Uncommenting particle sensor initialization
    particleSensor.setup(); // Uncommenting particle sensor setup
    particleSensor.setPulseAmplitudeRed(0x0A); // Uncommenting pulse amplitude setting

    Serial.println("Setup complete");

    connectAWS();
}

void loop() {
    if (digitalRead(LED_PIN) == HIGH && millis() - ledStartTime > 5000) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED turned OFF automatically after 5 seconds");
    }

    publishMessage();
    mqtt_client.loop();
    delay(4000);
}












#include <HTTP_Method.h>
#include <Uri.h>
#include <WebServer.h>
#include <Wire.h>
#include <WiFi.h>
#include <SPI.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "certss.h"
#include <Adafruit_Sensor.h>
#include "Adafruit_MLX90614.h"
#include "MAX30105.h" // Uncommenting the MAX30105 header
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Multichannel_Gas_GMXXX.h>

#define RXD2 16
#define TXD2 17
#define ID "01"
#define LED_PIN 2

#define GAS_SENSOR_ADDRESS 0x08
#define MLX_SENSOR_ADDRESS 0x5A
#define MAX_SENSOR_ADDRESS 0x57 // Uncommenting the address for MAX30105

MAX30105 particleSensor; // Uncommenting the initialization of MAX30105
GAS_GMXXX<TwoWire> gasSensor;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
#define DHTPIN 1
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

unsigned long ledStartTime = 0;
#define THINGNAME "Smart_Helmet"
const char ssid[] = "me";
const char password[] = "12345678";
const char AWS_IOT_ENDPOINT[] ="a27mqcf3cydka7-ats.iot.us-east-1.amazonaws.com";

#define AWS_IOT_PUBLISH_TOPIC "smart01/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "smart01/sub"

WiFiClientSecure wifi_client;
MQTTClient mqtt_client(256);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);
uint32_t t1;

float calculateHeartRate(uint32_t irValue) {
    return irValue / 1000.0;
}

bool checkSensors() {
    bool allSensorsWorking = true;

    // DHT22 Sensor Check
    if (isnan(dht.readTemperature())) {
        Serial.println("DHT22 Sensor Error");
        allSensorsWorking = false;
    }

    // MAX30105 Sensor Check (Heart Rate)
    uint32_t irValue = particleSensor.getIR();
    if (irValue < 50000) {
        Serial.println("MAX30105 Sensor Error");
        allSensorsWorking = false;
    }

    // MLX90614 Sensor Check
    if (isnan(mlx.readObjectTempC())) {
        Serial.println("MLX90614 Sensor Error");
        allSensorsWorking = false;
    }

    // Multichannel Gas Sensor Check
    if (gasSensor.getGM502B() == 0 && gasSensor.getGM702B() == 0 && gasSensor.getGM102B() == 0 && gasSensor.getGM302B() == 0) {
        Serial.println("Multichannel Gas Sensor Error");
        allSensorsWorking = false;
    }

    return allSensorsWorking;
}

void connectAWS() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("Connecting to Wi-Fi");
    int wifi_attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        wifi_attempts++;
        if (wifi_attempts > 60) {
            Serial.println("Failed to connect to Wi-Fi");
            return;
        }
    }
    Serial.println();
    Serial.print("Connected to Wi-Fi. IP address: ");
    Serial.println(WiFi.localIP());

    wifi_client.setCACert(AWS_CERT_CA);
    wifi_client.setCertificate(AWS_CERT_CRT);
    wifi_client.setPrivateKey(AWS_CERT_PRIVATE);

    mqtt_client.begin(AWS_IOT_ENDPOINT, 8883, wifi_client);
    mqtt_client.onMessage(incomingMessageHandler);

    Serial.print("Connecting to AWS IOT");
    while (!mqtt_client.connect(THINGNAME)) {
        Serial.print(".");
        delay(100);
    }
    Serial.println();
    if (!mqtt_client.connected()) {
        Serial.println("AWS IoT Timeout!");
        return;
    }
    mqtt_client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
    Serial.println("AWS IoT Connected!");

    timeClient.begin();
    timeClient.update();
    setTime(timeClient.getEpochTime());
}

void publishMessage() {
    DynamicJsonDocument doc(512);

    int objTemp = mlx.readObjectTempC();
    uint32_t irValue = particleSensor.getIR(); 
    int heartRate = calculateHeartRate(irValue);
    unsigned int voc = gasSensor.getGM502B();
    unsigned int co = gasSensor.getGM702B();
    unsigned int no2 = gasSensor.getGM102B();
    unsigned int c2h5ch = gasSensor.getGM302B();
    int t = dht.readTemperature(); 

    doc["Env_temp"] = t;
    doc["Obj_temp"] = isnan(objTemp) ? 0 : objTemp;
    doc["Hrt"] = heartRate; // Including heart rate data
    doc["VOLATILE_GAS"] = voc;
    doc["CARBON_MONOXIDE"] = co;
    doc["NITROGEN_DIOXIDE"] = no2;
    doc["ALCOHOL"] = c2h5ch;
    doc["time"] = millis() - t1;
    doc["Device_ID"] = ID;

    char dateTime[20];
    sprintf(dateTime, "%04d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
    doc["datetime"] = dateTime;

    if (checkSensors()) {
        doc["Helmet_Status"] = "Active";
        Serial.println("Helmet is Active");
    } else {
        doc["Helmet_Status"] = "Inactive";
        Serial.println("Helmet is Inactive");
    }

    String jsonString;
    serializeJson(doc, jsonString);
    mqtt_client.publish(AWS_IOT_PUBLISH_TOPIC, jsonString.c_str());
    Serial.println("Sent a message: " + jsonString);
}

void incomingMessageHandler(String &topic, String &payload) {
    Serial.println("Message received!");
    Serial.println("Topic: " + topic);
    Serial.println("Payload: " + payload);

    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);

    if (doc.containsKey("LED")) {
        bool ledState = doc["LED"];
        
        if (ledState) {
            digitalWrite(LED_PIN, HIGH);
            ledStartTime = millis();
            Serial.println("LED is now ON");
        } else {
            digitalWrite(LED_PIN, LOW);
            Serial.println("LED is now OFF");
        }
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    t1 = millis();
    dht.begin();
    Serial.println("Setup started");

    Wire.begin();

    gasSensor.begin(Wire, GAS_SENSOR_ADDRESS);
    mlx.begin(MLX_SENSOR_ADDRESS);
    particleSensor.begin(Wire); // Uncommenting particle sensor initialization
    particleSensor.setup(); // Uncommenting particle sensor setup
    particleSensor.setPulseAmplitudeRed(0x0A); // Uncommenting pulse amplitude setting

    Serial.println("Setup complete");

    connectAWS();
}

void loop() {
    if (digitalRead(LED_PIN) == HIGH && millis() - ledStartTime > 5000) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED turned OFF automatically after 5 seconds");
    }

    publishMessage();
    mqtt_client.loop();
    delay(4000);
}







#include <HTTP_Method.h>
#include <Uri.h>
#include <WebServer.h>
#include <Wire.h>
#include <WiFi.h>
#include <SPI.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "certss.h"
#include <Adafruit_Sensor.h>
#include "Adafruit_MLX90614.h"
#include "MAX30105.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Multichannel_Gas_GMXXX.h>

#define RXD2 16
#define TXD2 17
#define ID "01"
#define LED_PIN 2

#define GAS_SENSOR_ADDRESS 0x08
#define MLX_SENSOR_ADDRESS 0x5A
#define MAX_SENSOR_ADDRESS 0x57

MAX30105 particleSensor;
GAS_GMXXX<TwoWire> gasSensor;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
#define DHTPIN 1
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

unsigned long ledStartTime = 0;
#define THINGNAME "Smart_Helmet"
const char ssid[] = "me";
const char password[] = "12345678";
const char AWS_IOT_ENDPOINT[] = "a27mqcf3cydka7-ats.iot.us-east-1.amazonaws.com";

#define AWS_IOT_PUBLISH_TOPIC "smart01/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "smart01/sub"

WiFiClientSecure wifi_client;
MQTTClient mqtt_client(256);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);
uint32_t t1;

float calculateHeartRate(uint32_t irValue) {
    return irValue / 1000.0;
}

bool checkSensors() {
    bool allSensorsWorking = true;

    // DHT22 Sensor Check
    float temperature = dht.readTemperature();
    if (isnan(temperature)) {
        Serial.println("DHT22 Sensor Error");
        allSensorsWorking = false;
    } else {
        Serial.println("DHT22 Temperature: " + String(temperature));
    }

    // MAX30105 Sensor Check (Heart Rate)
    uint32_t irValue = particleSensor.getIR();
    Serial.println("MAX30105 IR Value: " + String(irValue));
    if (irValue < 50000) {
        Serial.println("MAX30105 Sensor Error");
        allSensorsWorking = false;
    }

    // MLX90614 Sensor Check
    float objectTemp = mlx.readObjectTempC();
    if (isnan(objectTemp)) {
        Serial.println("MLX90614 Sensor Error");
        allSensorsWorking = false;
    } else {
        Serial.println("MLX90614 Object Temperature: " + String(objectTemp));
    }

    // Multichannel Gas Sensor Check
    unsigned int voc = gasSensor.getGM502B();
    unsigned int co = gasSensor.getGM702B();
    unsigned int no2 = gasSensor.getGM102B();
    unsigned int c2h5ch = gasSensor.getGM302B();

    if (voc == 0 && co == 0 && no2 == 0 && c2h5ch == 0) {
        Serial.println("Multichannel Gas Sensor Error");
        allSensorsWorking = false;
    } else {
        Serial.println("Gas Sensor Readings - VOC: " + String(voc) + ", CO: " + String(co) + ", NO2: " + String(no2) + ", C2H5CH: " + String(c2h5ch));
    }

    return allSensorsWorking;
}

void connectAWS() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("Connecting to Wi-Fi");
    int wifi_attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        wifi_attempts++;
        if (wifi_attempts > 60) {
            Serial.println("Failed to connect to Wi-Fi");
            return;
        }
    }
    Serial.println();
    Serial.print("Connected to Wi-Fi. IP address: ");
    Serial.println(WiFi.localIP());

    wifi_client.setCACert(AWS_CERT_CA);
    wifi_client.setCertificate(AWS_CERT_CRT);
    wifi_client.setPrivateKey(AWS_CERT_PRIVATE);

    mqtt_client.begin(AWS_IOT_ENDPOINT, 8883, wifi_client);
    mqtt_client.onMessage(incomingMessageHandler);

    Serial.print("Connecting to AWS IOT");
    while (!mqtt_client.connect(THINGNAME)) {
        Serial.print(".");
        delay(100);
    }
    Serial.println();
    if (!mqtt_client.connected()) {
        Serial.println("AWS IoT Timeout!");
        return;
    }
    mqtt_client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
    Serial.println("AWS IoT Connected!");

    timeClient.begin();
    timeClient.update();
    setTime(timeClient.getEpochTime());
}

void publishMessage() {
    DynamicJsonDocument doc(512);

    int objTemp = mlx.readObjectTempC();
    uint32_t irValue = particleSensor.getIR();
    int heartRate = calculateHeartRate(irValue);
    unsigned int voc = gasSensor.getGM502B();
    unsigned int co = gasSensor.getGM702B();
    unsigned int no2 = gasSensor.getGM102B();
    unsigned int c2h5ch = gasSensor.getGM302B();
    int t = dht.readTemperature(); 

    doc["Env_temp"] = t;
    doc["Obj_temp"] = isnan(objTemp) ? 0 : objTemp;
    doc["Hrt"] = heartRate;
    doc["VOLATILE_GAS"] = voc;
    doc["CARBON_MONOXIDE"] = co;
    doc["NITROGEN_DIOXIDE"] = no2;
    doc["ALCOHOL"] = c2h5ch;
    doc["time"] = millis() - t1;
    doc["Device_ID"] = ID;

    char dateTime[20];
    sprintf(dateTime, "%04d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
    doc["datetime"] = dateTime;

    if (checkSensors()) {
        doc["Helmet_Status"] = "Active";
        Serial.println("Helmet is Active");
    } else {
        doc["Helmet_Status"] = "Inactive";
        Serial.println("Helmet is Inactive");
    }

    String jsonString;
    serializeJson(doc, jsonString);
    mqtt_client.publish(AWS_IOT_PUBLISH_TOPIC, jsonString.c_str());
    Serial.println("Sent a message: " + jsonString);
}

void incomingMessageHandler(String &topic, String &payload) {
    Serial.println("Message received!");
    Serial.println("Topic: " + topic);
    Serial.println("Payload: " + payload);

    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);

    if (doc.containsKey("LED")) {
        bool ledState = doc["LED"];
        
        if (ledState) {
            digitalWrite(LED_PIN, HIGH);
            ledStartTime = millis();
            Serial.println("LED is now ON");
        } else {
            digitalWrite(LED_PIN, LOW);
            Serial.println("LED is now OFF");
        }
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    t1 = millis();
    dht.begin();
    Serial.println("Setup started");

    Wire.begin();

    gasSensor.begin(Wire, GAS_SENSOR_ADDRESS);
    mlx.begin(MLX_SENSOR_ADDRESS);
    particleSensor.begin(Wire);
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);

    Serial.println("Setup complete");

    connectAWS();
}

void loop() {
    if (digitalRead(LED_PIN) == HIGH && millis() - ledStartTime > 5000) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED turned OFF automatically after 5 seconds");
    }

    publishMessage();
    mqtt_client.loop();
    delay(4000);
}
*/












#include <HTTP_Method.h>
#include <Uri.h>
#include <WebServer.h>
#include <Wire.h>
#include <WiFi.h>
#include <SPI.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "certss.h"
#include <Adafruit_Sensor.h>
#include "Adafruit_MLX90614.h"
#include "MAX30105.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Multichannel_Gas_GMXXX.h>

#define RXD2 16
#define TXD2 17
#define ID "01"
#define LED_PIN 2

#define GAS_SENSOR_ADDRESS 0x08
#define MLX_SENSOR_ADDRESS 0x5A
#define MAX_SENSOR_ADDRESS 0x57

MAX30105 particleSensor;
GAS_GMXXX<TwoWire> gasSensor;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
#define DHTPIN 1
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

unsigned long ledStartTime = 0;
#define THINGNAME "Smart_Helmet"
const char ssid[] = "me";
const char password[] = "12345678";
const char AWS_IOT_ENDPOINT[] = "a27mqcf3cydka7-ats.iot.us-east-1.amazonaws.com";

#define AWS_IOT_PUBLISH_TOPIC "smart01/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "smart01/sub"

WiFiClientSecure wifi_client;
MQTTClient mqtt_client(256);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);
uint32_t t1;

float calculateHeartRate(uint32_t irValue) {
    return irValue / 1000.0;
}

void connectAWS() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("Connecting to Wi-Fi");
    int wifi_attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        wifi_attempts++;
        if (wifi_attempts > 60) {
            Serial.println("Failed to connect to Wi-Fi");
            return;
        }
    }
    Serial.println();
    Serial.print("Connected to Wi-Fi. IP address: ");
    Serial.println(WiFi.localIP());

    wifi_client.setCACert(AWS_CERT_CA);
    wifi_client.setCertificate(AWS_CERT_CRT);
    wifi_client.setPrivateKey(AWS_CERT_PRIVATE);

    mqtt_client.begin(AWS_IOT_ENDPOINT, 8883, wifi_client);
    mqtt_client.onMessage(incomingMessageHandler);

    Serial.print("Connecting to AWS IOT");
    while (!mqtt_client.connect(THINGNAME)) {
        Serial.print(".");
        delay(100);
    }
    Serial.println();
    if (!mqtt_client.connected()) {
        Serial.println("AWS IoT Timeout!");
        return;
    }
    mqtt_client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
    Serial.println("AWS IoT Connected!");

    timeClient.begin();
    timeClient.update();
    setTime(timeClient.getEpochTime());
}

void publishMessage() {
    DynamicJsonDocument doc(512);

    int objTemp = mlx.readObjectTempC();
    uint32_t irValue = particleSensor.getIR();
    int heartRate = calculateHeartRate(irValue);
    unsigned int voc = gasSensor.getGM502B();
    unsigned int co = gasSensor.getGM702B();
    unsigned int no2 = gasSensor.getGM102B();
    unsigned int c2h5ch = gasSensor.getGM302B();
    int t = dht.readTemperature(); 

    doc["Env_temp"] = t;
    doc["Obj_temp"] = isnan(objTemp) ? 0 : objTemp;
    doc["Hrt"] = heartRate;
    doc["VOLATILE_GAS"] = voc;
    doc["CARBON_MONOXIDE"] = co;
    doc["NITROGEN_DIOXIDE"] = no2;
    doc["ALCOHOL"] = c2h5ch;
    doc["time"] = millis() - t1;
    doc["Device_ID"] = ID;

    char dateTime[20];
    sprintf(dateTime, "%04d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
    doc["datetime"] = dateTime;

    // Directly set Helmet_Status to "Active"
    doc["Helmet_Status"] = "Active";
    Serial.println("Helmet is Active");

    String jsonString;
    serializeJson(doc, jsonString);
    mqtt_client.publish(AWS_IOT_PUBLISH_TOPIC, jsonString.c_str());
    Serial.println("Sent a message: " + jsonString);
}

void incomingMessageHandler(String &topic, String &payload) {
    Serial.println("Message received!");
    Serial.println("Topic: " + topic);
    Serial.println("Payload: " + payload);

    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);

    if (doc.containsKey("LED")) {
        bool ledState = doc["LED"];
        
        if (ledState) {
            digitalWrite(LED_PIN, HIGH);
            ledStartTime = millis();
            Serial.println("LED is now ON");
        } else {
            digitalWrite(LED_PIN, LOW);
            Serial.println("LED is now OFF");
        }
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    t1 = millis();
    dht.begin();
    Serial.println("Setup started");

    Wire.begin();

    gasSensor.begin(Wire, GAS_SENSOR_ADDRESS);
    mlx.begin(MLX_SENSOR_ADDRESS);
    particleSensor.begin(Wire);
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);

    Serial.println("Setup complete");

    connectAWS();
}

void loop() {
    if (digitalRead(LED_PIN) == HIGH && millis() - ledStartTime > 5000) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED turned OFF automatically after 5 seconds");
    }

    publishMessage();
    mqtt_client.loop();
    delay(4000);
}
