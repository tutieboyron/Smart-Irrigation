// ===================== INCLUDES =====================
#include <WiFi.h>                     // WiFi connectivity
#include <PubSubClient.h>             // MQTT client for AWS IoT
#include <ArduinoJson.h>              // JSON parsing & creation
#include <WiFiClientSecure.h>         // TLS-secured WiFi client
#include <time.h>                     // NTP for time sync

// ===================== WIFI CONFIG =====================
const char* ssid = "G4";              // My WiFi name
const char* password = "dofu0595";    // My WiFi password

// ===================== AWS CONFIG =====================
const char* aws_endpoint = "am1sf2x7adcz9-ats.iot.us-east-1.amazonaws.com"; // My AWS IoT endpoint
const int aws_port = 8883;            // Secure MQTT port
const char* client_id = "irrigation-001"; // Unique device ID

// ===================== MQTT TOPICS =====================
const char* irrigation_command_topic = "agrisense/devices/irrigation-001/irrigation";      // Where I receive commands
const char* irrigation_response_topic = "agrisense/devices/irrigation-001/irrigation/response"; // Where I send back confirmation
const char* device_status_topic = "agrisense/devices/irrigation-001/status";               // Where I publish my status

// ===================== MQTT CONFIGURATION (FROM SENSOR) =====================
const int MQTT_KEEPALIVE_SECONDS = 60;  // 60 seconds
const int MQTT_SOCKET_TIMEOUT_SECONDS = 30;  // 30 seconds
const int MQTT_RETRY_DELAY = 1000;  // 1 second between retries
const int MQTT_MAX_RETRIES = 3;  // Maximum retry attempts

// ===================== TIME CONFIGURATION (FROM SENSOR) =====================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

// ===================== STRUCTURE DEFINITIONS =====================
struct IrrigationZone {
  int relayPin;            // GPIO pin connected to relay
  bool isActive;           // Whether irrigation is running
  unsigned long startTime; // When irrigation started
  unsigned long duration;  // How long to irrigate (in ms)
  String name;             // Zone label for status reporting
};

// ===================== RELAY & ZONES =====================
#define RELAY1 26               // Using GPIO26 for relay
#define LED_PIN 2               // Status LED
#define ZONE_COUNT 1            // I'm using only one zone for now

IrrigationZone zones[ZONE_COUNT] = {
  {RELAY1, false, 0, 0, "Zone 1"} // Initialize zone config
};

// ===================== CLIENT SETUP =====================
WiFiClientSecure wifiClient;         // TLS client for AWS
PubSubClient mqttClient(wifiClient); // MQTT client with TLS

// ===================== STATUS MANAGEMENT (SIMPLIFIED) =====================
struct DeviceState {
  bool isOnline = false;
  bool anyZoneActive = false;
  unsigned long lastReading = 0;
  time_t currentTime = 0;
  int freeMemory = 0;
  int rssi = 0;
} deviceState;

// ===================== TIMING CONTROL =====================
unsigned long lastStatusUpdate = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 30000; // Send status every 30 seconds
unsigned long lastConnectionCheck = 0;
const unsigned long CONNECTION_CHECK_INTERVAL = 5000; // Check connection every 5 seconds

// ===================== CONNECTION STATE =====================
bool connectionEstablished = false;
bool initialDataSent = false;

// ===================== AWS CERTIFICATES =====================
const char* device_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDWTCCAkGgAwIBAgIUVfpDicleWxZclJphhmmXzJYnaD4wDQYJKoZIhvcNAQEL
BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g
SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI1MDgwNDE2NDI1
NFoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0
ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAP0DBmpaABoYyob4OP6p
QAzNcVCE343bykzp3gAz/q8U9rDhH9TntwJl/0I+oG46qQQxQ0r/ZcGtqXvh7VEz
Vu9O95qXHtgj4n2fRKfSYtazwKytnoz/JlKdqv2oKh/0hsKQI1pjmu8TRJeUHpfr
Is9CJDA5Dbf3buSUQ1S8CVqxdUgs1oFqkdONTGevcPo2xeYBj561ZWHXYwgAoKYm
eswIeB5Ja403Y0bUCenaisA56im2ud02N499KJQjuOgnJEwR7Uh+XCsftNDb6o+j
GZrRAqyY0pIDllwTc6eq8BeueI87oFcjIiCX8VjWnE+FbI0sA7JyqddIvQbOd74e
IfMCAwEAAaNgMF4wHwYDVR0jBBgwFoAUnUb60OoYZiMyNkY1FtLgsDRw0G8wHQYD
VR0OBBYEFBSF1XDzJ6Kv0z+nVEjpdfi719UZMAwGA1UdEwEB/wQCMAAwDgYDVR0P
AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQAdrZBC/rZhULVUyJ1jCyJGjUkM
3aWwrU1AgZ6xLEn8TUS1keHjSmylwsao/QlBZCUKlY8G/MJIQd6FVykWPF2nkoWF
Tx1tTrRG1nBWt715yfHTlJf4SCWCNz6fjX6WVelRuEWK677ivP6WcjA6l2H0jctk
kb6DFej6k4dgPhcUltkHcWns1hz9N/kXXCZKrgdNd3q4Jq90m4C8P47LzFv/YyPf
HZYUEFm6xsglG50n4SYiO3ynFFS8DphVnW+4h20QzisvGK//283s8jlCjty/63n/
y7wnVDcZSEiyL2XvhZDqNWI/wY+q9TJmplAmlJACafyXuZdkX09dGcqai0Vg
-----END CERTIFICATE-----
)EOF";

const char* private_key = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEA/QMGaloAGhjKhvg4/qlADM1xUITfjdvKTOneADP+rxT2sOEf
1Oe3AmX/Qj6gbjqpBDFDSv9lwa2pe+HtUTNW7073mpce2CPifZ9Ep9Ji1rPArK2e
jP8mUp2q/agqH/SGwpAjWmOa7xNEl5Qel+siz0IkMDkNt/du5JRDVLwJWrF1SCzW
gWqR041MZ69w+jbF5gGPnrVlYddjCACgpiZ6zAh4HklrjTdjRtQJ6dqKwDnqKba5
3TY3j30olCO46CckTBHtSH5cKx+00Nvqj6MZmtECrJjSkgOWXBNzp6rwF654jzug
VyMiIJfxWNacT4VsjSwDsnKp10i9Bs53vh4h8wIDAQABAoIBAHLgOL1496rhrrLx
aQ9XUVl+vgBNFCOYs5WQade1w+FCp29QB9/nBVT1cpxlSvpBcBQTOfaHV3BEpTs2
LUY9BS/Krully/9e66sV8VoxT+cj3kuI2QAzqPbB160r+VRg9f7I6wRTTTlXikE4
Y1uey0NHSJ8MiIoKsjLejmOerLCcEwRzMBqGTRdq1M+UUycS/UM27v22acWV0xIj
iw8VTgoLN7V6EeuSRKudqqOIHDhrFLDwrE4wBGafF49fvMqJ7SQGWzM080vvkP+E
qPs8/NDES6L/fdKXWxRtpLucs1y7l0zBecTz1K9lYk2ebmi4UXc6HC9yGcAQYOnv
YbwgNuECgYEA/rIehhfsQ5JqUT/vJ0F12ouAaYrJETeWTRUl4OUOtRj71uHlgiEn
BZH0tMeBPxhFNUmIXk/qJckxcCITDhLh4yODQCwJyC8AHV522Hnqwlc0NJAG3kIh
Bu7ql62qeWDsj1sXUBefO0yU0WYbbcLSnyK4HC0YrtzOcN+UosrFx70CgYEA/k6y
xSlTkVhvYGDVON1IMeG2XfWX0Hc0qCu3kYPD7paCjINelTu2M4i8CiMvfE/UYgr6
uiEZZBF+OGg6nZlMnyiYmAlsb1yPHeg+773HAB1er48JOTlMvr8XXgw6fkXA4NaZ
mz+AYJ+MdBALgId6hJnyV/hOcRDllunLtlyHk28CgYEAtbQXaKz6hn6XUN3e3U+r
J74sX59+sMTKerWir15a7sIjMPs6BGtobmXhgXNvdrb28PxDyJ0Uu95cYdY9Z+X6
t1QnmAmS9PjrCQjSmr/nxrv156X08G2XKF4ciReBrlSCkAd5i08+70xEQ1uoJ9c8
8gEP/jICEQZAVXB8luM41LUCgYEAtpQx7TO6d8OSUxOygdz9FDMFB3hnwpeTCpo4
dRSw+v68Q72djm8MZPtqZazVTt7RkIJpeHCDFkEo6b6LNtL1G9+9jopVe65sYgB4
Dw+lbAOqE0kSSi9FVj1DvZXx+O6Dh+kK51c0CCsEX9+VInYiFEmioF35k1uaOjso
sojEHakCgYBvYaB+JEbmczm6LTYRCBg2r2pmKawz9JturNhsJ5ueCgg1t8LUmIbQ
PWuGAjBkcakdTRbrdIbxJagx6b4cdQnT/wo+weEp/gZ2rbgJqGnH3RSXnTmjw5M2
ku3RwF3fYEiClvdpKGTUbp9NFyr5VGJZ0+StuVj9uzccZ5T4BCQLCw==
-----END RSA PRIVATE KEY-----
)EOF";

const char* root_ca = R"EOF(
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

// ===================== ARDUINO SETUP (SENSOR-STYLE) =====================
void setup() {
  Serial.begin(115200);
  delay(500); // Shorter initial delay
  
  Serial.println("AgriSense Irrigation Controller - Robust Version");
  Serial.println("===============================================");
  
  // Setup pins
  pinMode(RELAY1, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY1, HIGH); // Relay OFF initially (active-low)
  digitalWrite(LED_PIN, HIGH); // LED ON during setup
  Serial.println("✓ Relay and LED pins configured");

  // INCREASE MQTT BUFFER SIZE - Critical fix from sensor code!
  mqttClient.setBufferSize(1024);
  Serial.print("✓ MQTT buffer size set to: ");
  Serial.println(mqttClient.getBufferSize());

  // Test connectivity (sensor-style approach)
  bool connected = ensureConnectivity();

  if (connected) {
    Serial.println("\n=== SENDING INITIAL DATA ===");
    
    // Update device state
    updateDeviceState();
    
    // Send initial registration and status
    sendDeviceRegistrationWithRetry();
    delay(500); // Small delay between messages
    sendDeviceStatusWithRetry();
    
    Serial.println("✓ Initial data sent successfully");
    initialDataSent = true;
  } else {
    Serial.println("⚠ Connection failed - will retry in main loop");
  }

  digitalWrite(LED_PIN, LOW); // LED OFF after setup
  Serial.println("=== SETUP COMPLETE ===");
}

// ===================== SIMPLIFIED MAIN LOOP (SENSOR-INSPIRED) =====================
void loop() {
  // Always check irrigation timers regardless of connection
  checkIrrigationTimers();

  // Periodic connection check (every 5 seconds)
  if (millis() - lastConnectionCheck > CONNECTION_CHECK_INTERVAL) {
    checkAndMaintainConnection();
    lastConnectionCheck = millis();
  }

  // Process MQTT messages if connected
  if (mqttClient.connected()) {
    mqttClient.loop();
    
    // Send periodic status updates
    if (initialDataSent && millis() - lastStatusUpdate > STATUS_UPDATE_INTERVAL) {
      updateDeviceState();
      sendDeviceStatusWithRetry();
      lastStatusUpdate = millis();
    }
  }

  delay(100); // Small delay to prevent overwhelming the system
}

// ===================== CONNECTIVITY FUNCTIONS (FROM SENSOR) =====================
bool ensureConnectivity() {
  connectToWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠ Wi-Fi not connected.");
    return false;
  }

  // Configure time (sensor approach)
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("⏰ Waiting for time sync...");
  delay(2000); // Give time for NTP sync

  connectToAWSIoT();
  if (!mqttClient.connected()) {
    Serial.println("⚠ MQTT connection failed.");
    return false;
  }

  Serial.println("✅ Connected!");
  connectionEstablished = true;
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
    Serial.println("\n✓ WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n✗ WiFi failed!");
  }
}

void connectToAWSIoT() {
  // Configure TLS credentials
  wifiClient.setCACert(root_ca);
  wifiClient.setCertificate(device_cert);
  wifiClient.setPrivateKey(private_key);

  // Configure MQTT settings (from sensor)
  mqttClient.setServer(aws_endpoint, aws_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(MQTT_KEEPALIVE_SECONDS);
  mqttClient.setSocketTimeout(MQTT_SOCKET_TIMEOUT_SECONDS);
  
  Serial.print("Connecting to AWS IoT Core: ");
  Serial.println(aws_endpoint);

  if (mqttClient.connect(client_id)) {
    Serial.println("✓ Connected to AWS IoT Core!");
    deviceState.isOnline = true;
    
    // Always re-subscribe after connection (CRITICAL FIX)
    resubscribeToTopics();
  } else {
    Serial.print("✗ AWS IoT failed! Error: ");
    Serial.println(mqttClient.state());
    deviceState.isOnline = false;
  }
}

// ===================== SUBSCRIPTION MANAGEMENT =====================
void resubscribeToTopics() {
  Serial.println("Re-subscribing to command topics...");
  
  // Subscribe to command topic with retry
  for (int attempt = 1; attempt <= 3; attempt++) {
    if (mqttClient.subscribe(irrigation_command_topic)) {
      Serial.println("✅ Subscribed to: " + String(irrigation_command_topic));
      return; // Success!
    } else {
      Serial.printf("❌ Subscription attempt %d failed\n", attempt);
      if (attempt < 3) {
        delay(500); // Wait before retry
      }
    }
  }
  Serial.println("⚠ All subscription attempts failed!");
}

// ===================== CONNECTION MAINTENANCE =====================
void checkAndMaintainConnection() {
  // Check WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    connectToWiFi();
    connectionEstablished = false;
    return;
  }

  // Check MQTT
  if (!mqttClient.connected()) {
    Serial.println("MQTT disconnected. Reconnecting...");
    connectToAWSIoT();
    if (!mqttClient.connected()) {
      connectionEstablished = false;
      deviceState.isOnline = false;
    } else {
      connectionEstablished = true;
      deviceState.isOnline = true;
    }
  }
}

// ===================== DEVICE STATE UPDATE =====================
void updateDeviceState() {
  deviceState.lastReading = millis();
  deviceState.freeMemory = ESP.getFreeHeap();
  deviceState.rssi = WiFi.RSSI();
  deviceState.isOnline = mqttClient.connected();
  
  // Check if any zone is active
  deviceState.anyZoneActive = false;
  for (int i = 0; i < ZONE_COUNT; i++) {
    if (zones[i].isActive) {
      deviceState.anyZoneActive = true;
      break;
    }
  }
}

// ===================== PUBLISH FUNCTIONS (SENSOR-STYLE) =====================
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

void sendDeviceRegistrationWithRetry() {
  Serial.println("Sending device registration:");
  
  // Smaller JSON document (sensor-style)
  StaticJsonDocument<512> doc;
  doc["deviceId"] = client_id;
  doc["timestamp"] = getCurrentTimestamp();
  doc["name"] = "Irrigation Controller (irrigation-001)";
  doc["farmId"] = "a8052ec1-26ca-4aec-9eaf-460f4a8b2428";
  doc["ownerId"] = "34983428-1031-7034-f4f4-d81f9d0b76a8";
  doc["isActuator"] = true;
  doc["type"] = "irrigation_controller";
  doc["status"] = "Online";
  doc["firmwareVersion"] = "1.0.3-robust";
  
  // Essential Lambda compatibility fields
  doc["deviceType"] = "irrigation_controller";
  doc["deviceName"] = "Irrigation Controller (irrigation-001)";
  doc["actuatorState"] = deviceState.anyZoneActive ? "ON" : "OFF";
  doc["serialNumber"] = client_id;
  doc["version"] = 1;
  doc["_typename"] = "Device";

  String jsonString;
  serializeJson(doc, jsonString);
  Serial.println(jsonString);
  
  publishWithRetry("agrisense/devices/irrigation-001/register", jsonString.c_str(), "device registration");
}

void sendDeviceStatusWithRetry() {
  Serial.println("Sending device status:");
  
  // Optimized JSON size (sensor-style)
  StaticJsonDocument<512> doc;
  doc["deviceId"] = client_id;
  doc["timestamp"] = getCurrentTimestamp();
  doc["status"] = deviceState.isOnline ? "Online" : "Offline";
  doc["lastReadingAt"] = getCurrentTimestamp();
  doc["firmwareVersion"] = "1.0.3-robust";
  
  // Essential fields only
  doc["ownerId"] = "34983428-1031-7034-f4f4-d81f9d0b76a8";
  doc["farmId"] = "a8052ec1-26ca-4aec-9eaf-460f4a8b2428";
  doc["deviceType"] = "irrigation_controller";
  doc["deviceName"] = "Irrigation Controller (irrigation-001)";
  doc["isActuator"] = true;
  doc["actuatorState"] = deviceState.anyZoneActive ? "ON" : "OFF";
  doc["serialNumber"] = client_id;
  doc["freeMemory"] = deviceState.freeMemory;
  doc["rssi"] = deviceState.rssi;
  doc["_typename"] = "Device";

  String jsonString;
  serializeJson(doc, jsonString);
  Serial.println(jsonString);
  
  publishWithRetry(device_status_topic, jsonString.c_str(), "device status");
}

// ===================== MQTT CALLBACK =====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received on topic: ");
  Serial.println(topic);

  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';

  Serial.print("Message: ");
  Serial.println(message);

  StaticJsonDocument<256> doc; // Smaller document for commands
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.c_str());
    return;
  }

  if (strcmp(topic, irrigation_command_topic) == 0) {
    handleIrrigationCommand(doc);
  }
}

// ===================== IRRIGATION CONTROL =====================
void handleIrrigationCommand(JsonDocument& doc) {
  const char* action = doc["action"];
  int duration = doc["duration"] | 0;
  int zone = doc["zone"] | 0;

  Serial.printf("Irrigation command: %s Zone: %d Duration: %d\n", action, zone, duration);

  if (strcmp(action, "START") == 0) {
    startIrrigation(zone, duration);
  } else if (strcmp(action, "STOP") == 0) {
    stopIrrigation(zone);
  } else if (strcmp(action, "STOP_ALL") == 0) {
    stopAllIrrigation();
  }

  publishIrrigationResponse(action, zone, duration);
}

void startIrrigation(int zone, int duration) {
  if (zone >= 0 && zone < ZONE_COUNT) {
    zones[zone].isActive = true;
    zones[zone].startTime = millis();
    zones[zone].duration = duration * 60000UL;
    digitalWrite(zones[zone].relayPin, LOW);
    Serial.printf("✓ Started irrigation on %s for %d minutes\n", zones[zone].name.c_str(), duration);
    
    // Update device state immediately
    updateDeviceState();
  } else {
    Serial.printf("✗ Invalid zone: %d\n", zone);
  }
}

void stopIrrigation(int zone) {
  if (zone >= 0 && zone < ZONE_COUNT) {
    zones[zone].isActive = false;
    digitalWrite(zones[zone].relayPin, HIGH);
    Serial.printf("✓ Stopped irrigation on %s\n", zones[zone].name.c_str());
    
    // Update device state immediately
    updateDeviceState();
  } else {
    Serial.printf("✗ Invalid zone: %d\n", zone);
  }
}

void stopAllIrrigation() {
  for (int i = 0; i < ZONE_COUNT; i++) {
    stopIrrigation(i);
  }
  Serial.println("✓ Stopped all irrigation zones");
}

void checkIrrigationTimers() {
  for (int i = 0; i < ZONE_COUNT; i++) {
    if (zones[i].isActive && millis() - zones[i].startTime >= zones[i].duration) {
      Serial.printf("⏰ Timer expired for %s\n", zones[i].name.c_str());
      stopIrrigation(i);
    }
  }
}

void publishIrrigationResponse(const char* action, int zone, int duration) {
  Serial.println("Sending irrigation response:");
  
  // Smaller response JSON (sensor-style)
  StaticJsonDocument<256> doc;
  doc["timestamp"] = getCurrentTimestamp();
  doc["action"] = action;
  doc["zone"] = zone;
  doc["duration"] = duration;
  doc["status"] = "executed";
  doc["deviceId"] = client_id;

  String jsonString;
  serializeJson(doc, jsonString);
  Serial.println(jsonString);
  
  publishWithRetry(irrigation_response_topic, jsonString.c_str(), "irrigation response");
}

// ===================== UTILITY FUNCTIONS =====================
String getCurrentTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "2024-01-01T00:00:00Z"; // Fallback timestamp
  }
  
  char timeString[64];
  strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(timeString);
}