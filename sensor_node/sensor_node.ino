#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "DHT.h"
#include <WiFiClientSecure.h>
#include <time.h>

// ----------- PIN CONFIGURATION ------------
#define DHTPIN 14   // DHT22 data pin (G14)
#define DHTTYPE DHT22  // sensor type
#define SOIL_PIN 34  // Analog input pin for soil sensor
#define LED_PIN 2     // LED for status feedback

// ----------- WIFI CONFIGURATION ------------
const char* ssid = "G4";
const char* password = "dofu0595";

// ----------- AWS IOT CONFIGURATION ------------
const char* aws_endpoint = "am1sf2x7adcz9-ats.iot.us-east-1.amazonaws.com";
const int aws_port = 8883;
const char* device_id = "group-esp32-001";

// ----------- MQTT TOPICS ------------
const char* sensor_data_topic = "agrisense/sensors/group-esp32-001/data";
const char* device_status_topic = "agrisense/devices/group-esp32-001/status";
const char* test_topic = "test/simple";  // Simple test topic

// ----------- TIME CONFIGURATION ------------
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

// ----------- SOIL SENSOR CALIBRATION ------------
const int SOIL_DRY = 2500;    // Value when sensor is dry (in air)
const int SOIL_WET = 2000;    // Value when sensor is wet (in water)
const int SOIL_ERROR = 2559;  // Threshold to detect disconnected sensor

// ----------- MQTT CONFIGURATION ------------
const int MQTT_KEEPALIVE_SECONDS = 60;  // 60 seconds
const int MQTT_SOCKET_TIMEOUT_SECONDS = 30;  // 30 seconds
const int MQTT_RETRY_DELAY = 1000;  // 1 second between retries
const int MQTT_MAX_RETRIES = 3;  // Maximum retry attempts

// ----------- AWS IOT CERTIFICATES ------------
const char* aws_root_ca = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

const char* aws_device_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDWTCCAkGgAwIBAgIURY5ZeXbtxXGySyAwcnDjLdgsbwkwDQYJKoZIhvcNAQEL
BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g
SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI1MDcxNDEyNDgw
MloXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0
ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALXpRA9sRWYXKVFcwd+Z
bc9VRB5PwNwaLx/2vSAg1bw+oJutmNZbUbjNSNkbPXfLCgrfrhR1CvdtnePa+/Lp
jr7kuzuvkbFshRl4Cr78y3G7XIE9/B0NhtBcEHczKs3kXww+bc35quZOAGVVj3lv
4xWh+RisYap9E3t/FNXVo7ifq0h597+gCULLZjrhP21bW/BsK7NXglx4Iyde+kpt
lNuSutdguM/nJZISxxb/bKaNc0HkuJNdZUK2D5245FrngfX3nUBseRRMk7s3ihdL
vGWVvqQhg2RYx3pkzgx5SfQyZ+76LL2+76hVqAgSy6XOjdDS/tyaaic/W+nyX1pY
+HECAwEAAaNgMF4wHwYDVR0jBBgwFoAUhNWveNfoTKGfIYIqumEtzowD9L8wHQYD
VR0OBBYEFEqzDLlEMvXZQLUGB8o+3gES6DLdMAwGA1UdEwEB/wQCMAAwDgYDVR0P
AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQC5ZgGNUJcVzs4IRv0ShmPLQ1n7
EUDDZOUcjVzn84O0IMl0zWKc2kOxclddag3WDCL+KRHYtOWbklmmATOCRjYIcWET
XpRHPXzeuGCaCtzb/69zDFqQm5SL3UDrTBwIQso3VCOemRh8+/7k3aQoOz3fxjXa
7tHELjVCvyZH5iIkHbgg0vvC8PgxkkZx6/43tSeMNylvCWyLjA69+Wb1qax9Jw5c
7MaFcKtk2h3gmm6Jg6iRWmFwEydkHL7fyzfi4ELieMvz7bulHLbDeCd+9tHLmMMl
qxTLnrf6KbLyF3IGV36qEwMYDVrw41dk5KDew9o1WqIZrjipRWZW3bh8osCE
-----END CERTIFICATE-----
)EOF";

const char* aws_private_key = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEAtelED2xFZhcpUVzB35ltz1VEHk/A3BovH/a9ICDVvD6gm62Y
1ltRuM1I2Rs9d8sKCt+uFHUK922d49r78umOvuS7O6+RsWyFGXgKvvzLcbtcgT38
HQ2G0FwQdzMqzeRfDD5tzfmq5k4AZVWPeW/jFaH5GKxhqn0Te38U1dWjuJ+rSHn3
v6AJQstmOuE/bVtb8Gwrs1eCXHgjJ176Sm2U25K612C4z+clkhLHFv9spo1zQeS4
k11lQrYPnbjkWueB9fedQGx5FEyTuzeKF0u8ZZW+pCGDZFjHemTODHlJ9DJn7vos
vb7vqFWoCBLLpc6N0NL+3JpqJz9b6fJfWlj4cQIDAQABAoIBAQCO7HZn87ZW7s2s
ukcsydxn8OMzOZAanov1/iM78fBcFYyUpAEYMel0W/3nbiqOSz1Zq8KXGhqCqmJK
GsynXu+jdgzRaTD8CHpOt20h/3ZMpluYw72oie8pF5xsOwxfdhVjhk05bvbiOdi/
xUiTrd/qKDeRNicKD3lWJ1OUamVW8PEtNcBaNXwnqggpjgmB5wWMtdxu+DhNC01z
v/RlSZXqrChgdwU5Xt4LpFzFyObSjUmofOrFEMltJbNHkzx17fmaD3r54Xd9j2+p
aqoQ7SRoVHAnn1M69i3hx/uIJOu6jkVZYL1rGf/dLEvGX3y6Iy5V8/Ln1AF1Lm2l
CrfKbYRRAoGBANzsipvzSr3fTp/t+7fnuAgy+I+XbqRpg6oAvNh66qGb74xz0CrI
n+6HC0efMop5w3DfMyBhlMULpWZl/sWtUTBqSJPIR9p73HR8WcgzeC7Vj7lEs8BZ
avHodG5utpy9clXoBuqXeKHm6lXyUxMMzP1CE276EhGsA2qBqGMycquNAoGBANLL
DIAJk286qyCFCVm4/lPgKERtnzgexyNwFDAnIjQ7ooKAs7s+lp8iSPqK8CzEy2uV
b/xx3JmQMBrriZhvaxi2pi1agl6scxlZjY0DNPzO1lgzoqCNYrIi/Dl+B06jljGj
pOopD8sxFrd9ggQrh18uvNA5rsaZfiSCdBds1hV1AoGADeKUB58v6GUblPOpKXLX
3zQM6UF0Q8MZ8lpfhB+dlZCuap5wy++WFbDJJbQ8wfVLMlHk7bkUV5oWyCvK6nOt
MaTMcPVahsHYJj4Win2ppQ/pG5TU01cLYK7ienpc4dcKU5nkrWPdwhc4TTSQwhbA
334CqKvw7Mlp4YzKn4lxKZUCgYBTh6bSbfkAkK0TW+SSq14M+ry6MP5xDLE498bo
Nfm13RPOxxVx06F4OevVbI00EF/TqAoSbURPjfWiFUgXIb+8sVQ0kLMstQV/PB99
i5HJxGTn2r5NBPnhQT/VwH1Ayk5QKVrGd7MMf81StOd6o40nSKwFj3YUuOplqVQ7
nCNRqQKBgHttulD2P7ARBO6APsEh7xGaOqhKqYU0rh4VCRTgdocgy0GgkdL7fbyB
tH6w0cYpUkHx15ZYF9It3PYukmBiHiMdznqC2WpLKkjMykMRtOlBIHQgUUkioHR3
a0nOOGrjvaY3go84vUTZFwG/YiV37rdBzTWpWeQh/LzvJYCh9vSa
-----END RSA PRIVATE KEY-----
)EOF";

// ----------- SENSOR OBJECTS ---------------
DHT dht(DHTPIN, DHTTYPE);
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

struct DeviceState {
  bool isOnline = false;
  float temperature = 0;
  float humidity = 0;
  int soilMoisture = 0;
  int soilMoistureRaw = 0;
  bool soilSensorConnected = false;
  unsigned long lastReading = 0;
  time_t currentTime = 0;
} deviceState;

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.println("AgriSense ESP32 - Robust Version");
  Serial.println("=================================");

  dht.begin();

  // INCREASE MQTT BUFFER SIZE
  mqttClient.setBufferSize(1024);
  Serial.print("MQTT buffer size set to: ");
  Serial.println(mqttClient.getBufferSize());

  // Test connectivity
  bool connected = ensureConnectivity();

  if (connected) {
    Serial.println("\n=== READING SENSORS ===");
    readSensors();
    
    Serial.println("\n=== SENDING DATA ===");
    sendSensorDataWithRetry();
    sendDeviceStatusWithRetry();
    
    Serial.println("=== COMPLETE ===");
  } else {
    Serial.println("⚠ Connection failed - skipping data transmission");
  }

  digitalWrite(LED_PIN, LOW);

  Serial.println("💤 Going to deep sleep for 5 minutes...");
  esp_sleep_enable_timer_wakeup(300000000ULL); // 5 minutes
  esp_deep_sleep_start();
}

void loop() {}

bool ensureConnectivity() {
  connectToWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠ Wi-Fi not connected.");
    return false;
  }

  // Configure time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("⏰ Waiting for time sync...");
  delay(2000); // Give time for NTP sync

  connectToAWSIoT();
  if (!mqttClient.connected()) {
    Serial.println("⚠ MQTT connection failed.");
    return false;
  }

  Serial.println("✅ Connected!");
  return true;
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(300);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi failed!");
  }
}

void connectToAWSIoT() {
  wifiClient.setCACert(aws_root_ca);
  wifiClient.setCertificate(aws_device_cert);
  wifiClient.setPrivateKey(aws_private_key);

  mqttClient.setServer(aws_endpoint, aws_port);
  
  // Set MQTT connection parameters
  mqttClient.setKeepAlive(MQTT_KEEPALIVE_SECONDS);
  mqttClient.setSocketTimeout(MQTT_SOCKET_TIMEOUT_SECONDS);
  
  Serial.print("Connecting to AWS IoT Core: ");
  Serial.println(aws_endpoint);

  if (mqttClient.connect(device_id)) {
    Serial.println("Connected to AWS IoT Core!");
    deviceState.isOnline = true;
  } else {
    Serial.print("❌ AWS IoT failed! Error: ");
    Serial.println(mqttClient.state());
    deviceState.isOnline = false;
  }
}

void readSensors() {
  Serial.println("Reading sensors...");
  
  // Read DHT sensor
  deviceState.temperature = dht.readTemperature();
  deviceState.humidity = dht.readHumidity();
  
  // Read soil sensor
  deviceState.soilMoistureRaw = analogRead(SOIL_PIN);
  
  // Check if soil sensor is connected
  if (deviceState.soilMoistureRaw > SOIL_ERROR) {
    deviceState.soilSensorConnected = false;
    deviceState.soilMoisture = -1; // Indicate disconnected
    Serial.print("Soil moisture (raw): ");
    Serial.println(deviceState.soilMoistureRaw);
    Serial.println("⚠ Soil sensor likely disconnected or not in soil.");
  } else {
    deviceState.soilSensorConnected = true;
    // Convert raw value to percentage (0-100%)
    deviceState.soilMoisture = map(deviceState.soilMoistureRaw, SOIL_DRY, SOIL_WET, 0, 100);
    deviceState.soilMoisture = constrain(deviceState.soilMoisture, 0, 100);
    
    Serial.print("Soil moisture (raw): ");
    Serial.println(deviceState.soilMoistureRaw);
    Serial.print("Soil moisture (%): ");
    Serial.println(deviceState.soilMoisture);
  }
  
  deviceState.lastReading = millis();
}

String getCurrentTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "";
  }
  
  char timeString[512];
  strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(timeString);
}

bool publishWithRetry(const char* topic, const char* message, const char* operationName) {
  Serial.print("Message length: ");
  Serial.println(strlen(message));
  
  for (int attempt = 1; attempt <= MQTT_MAX_RETRIES; attempt++) {
    Serial.print("Attempt ");
    Serial.print(attempt);
    Serial.print("/");
    Serial.print(MQTT_MAX_RETRIES);
    Serial.print(": Publishing ");
    Serial.println(operationName);
    
    // Ensure we're still connected
    if (!mqttClient.connected()) {
      Serial.println("⚠ MQTT disconnected, reconnecting...");
      connectToAWSIoT();
      if (!mqttClient.connected()) {
        Serial.println("❌ Reconnection failed!");
        return false;
      }
    }
    
    // Process any pending MQTT messages
    mqttClient.loop();
    delay(100); // Small delay before publish
    
    // Attempt to publish
    if (mqttClient.publish(topic, message)) {
      Serial.println("✅ Successfully published!");
      return true;
    } else {
      Serial.print("❌ Publish failed. MQTT State: ");
      Serial.print(mqttClient.state());
      Serial.print(", Connected: ");
      Serial.println(mqttClient.connected());
      
      if (attempt < MQTT_MAX_RETRIES) {
        Serial.print("Retrying in ");
        Serial.print(MQTT_RETRY_DELAY);
        Serial.println("ms...");
        delay(MQTT_RETRY_DELAY);
      }
    }
  }
  
  Serial.println("❌ All retry attempts failed!");
  return false;
}

void sendSensorDataWithRetry() {
  Serial.println("Sending sensor data:");
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = device_id;
  doc["timestamp"] = getCurrentTimestamp();
  doc["temperature"] = deviceState.temperature;
  doc["humidity"] = deviceState.humidity;
  doc["soilMoisture"] = deviceState.soilMoisture;
  
  // Add Lambda compatibility fields
  doc["ownerId"] = "34983428-1031-7034-f4f4-d81f9d0b76a8";
  doc["farmId"] = "a8052ec1-26ca-4aec-9eaf-460f4a8b2428";
  doc["deviceType"] = "esp32_multi_sensor";
  doc["deviceName"] = "ESP32 Sensor Node (group-esp32-001)";
  doc["firmwareVersion"] = "1.0.0";
  doc["isActuator"] = false;
  doc["actuatorState"] = "OFF";
  doc["serialNumber"] = device_id;
  doc["status"] = "Online";
  doc["batteryLevel"] = nullptr;
  doc["version"] = 1;
  doc["_deleted"] = nullptr;
  doc["_lastChangedAt"] = millis();
  doc["_typename"] = "SensorData";

  String jsonString;
  serializeJson(doc, jsonString);
  Serial.println(jsonString);
  
  publishWithRetry(sensor_data_topic, jsonString.c_str(), "sensor data");
}

void sendDeviceStatusWithRetry() {
  Serial.println("Sending device status:");
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = device_id;
  doc["timestamp"] = getCurrentTimestamp();
  doc["isOnline"] = deviceState.isOnline;
  doc["temperature"] = deviceState.temperature;
  doc["humidity"] = deviceState.humidity;
  doc["soilMoisture"] = deviceState.soilMoisture;
  doc["soilMoistureRaw"] = deviceState.soilMoistureRaw;
  doc["soilSensorConnected"] = deviceState.soilSensorConnected;
  doc["freeMemory"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();
  
  // Add Lambda compatibility fields
  doc["ownerId"] = "34983428-1031-7034-f4f4-d81f9d0b76a8";
  doc["farmId"] = "a8052ec1-26ca-4aec-9eaf-460f4a8b2428";
  doc["deviceType"] = "esp32_multi_sensor";
  doc["deviceName"] = "ESP32 Sensor Node (group-esp32-001)";
  doc["firmwareVersion"] = "1.0.0";
  doc["isActuator"] = false;
  doc["actuatorState"] = "OFF";
  doc["serialNumber"] = device_id;
  doc["status"] = "Online";
  doc["batteryLevel"] = nullptr;
  doc["version"] = 1;
  doc["_deleted"] = nullptr;
  doc["_lastChangedAt"] = millis();
  doc["_typename"] = "Device";

  String jsonString;
  serializeJson(doc, jsonString);
  Serial.println(jsonString);
  
  publishWithRetry(device_status_topic, jsonString.c_str(), "device status");
}