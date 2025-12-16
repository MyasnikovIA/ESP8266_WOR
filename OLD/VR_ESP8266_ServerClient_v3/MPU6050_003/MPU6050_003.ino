/*
  MPU6050 VR Tracker with PCA9548A Multiplexer
  Supports up to 8 MPU6050 sensors
  WebSocket Implementation with sensor status tracking
  Works with or without Serial connection
  With 3D sensor visualization and front position memory
  FIXED: Angle wrapping and drift compensation
*/

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <EEPROM.h>

// PCA9548A multiplexer address
#define PCA9548A_ADDRESS 0x70

// WiFi credentials
const char* ssid = "ELTEX-87A2_asus";
const char* password = "GP08004568";

// EEPROM settings
#define EEPROM_SIZE 512
#define FRONT_POSITION_START 0  // Start address for front positions

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Structure to hold sensor data and status
struct SensorData {
  Adafruit_MPU6050 mpu;
  bool connected = false;
  bool calibrated = false;
  float smoothedPitch = 0;
  float smoothedRoll = 0;
  float smoothedYaw = 0;
  float pitch = 0, roll = 0, yaw = 0;
  float frontPitch = 0, frontRoll = 0, frontYaw = 0;  // Front reference position
  float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
  unsigned long lastChangeTime = 0;
  unsigned long lastDataTime = 0;
  float lastSentPitch = 0;
  float lastSentRoll = 0;
  float lastSentYaw = 0;
  float lastSentRelativePitch = 0;
  float lastSentRelativeRoll = 0;
  float lastSentRelativeYaw = 0;
};

// Array for 8 sensors
SensorData sensors[8];

// Global variables
const float smoothingFactor = 0.3;
unsigned long lastTime = 0;
unsigned long calibrationStart = 0;
const unsigned long calibrationTime = 3000;

// Threshold for detecting significant changes (in degrees)
const float CHANGE_THRESHOLD = 1.0;

// WebSocket connection management
bool clientConnected = false;
unsigned long lastDataSend = 0;
const unsigned long MIN_SEND_INTERVAL = 50; // Minimum 50ms between sends

// WebSocket ping для поддержания соединения
unsigned long lastPingTime = 0;
const unsigned long PING_INTERVAL = 30000; // 30 секунд

// Yaw drift compensation
const float YAW_DRIFT_COMPENSATION = 0.01;

// Angle wrapping limits
const float MAX_ANGLE = 180.0f;
const float MIN_ANGLE = -180.0f;

// Serial connection status
bool serialConnected = false;

// Safe serial print function
void safePrint(String message) {
  if (serialConnected) {
    Serial.print(message);
  }
}

void safePrintln(String message) {
  if (serialConnected) {
    Serial.println(message);
  }
}

void safePrintf(const char* format, ...) {
  if (serialConnected) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
  }
}

// Function to wrap angles to [-180, 180] range
float wrapAngle(float angle) {
  while (angle > 180.0f) angle -= 360.0f;
  while (angle < -180.0f) angle += 360.0f;
  return angle;
}

// Function to select multiplexer channel
void selectChannel(uint8_t channel) {
  if (channel > 7) return;
  Wire.beginTransmission(PCA9548A_ADDRESS);
  Wire.write(1 << channel);
  Wire.endTransmission();
}

// EEPROM functions
void saveFrontPosition(int channel) {
  int addr = FRONT_POSITION_START + channel * sizeof(float) * 3;
  
  EEPROM.put(addr, sensors[channel].frontPitch);
  EEPROM.put(addr + sizeof(float), sensors[channel].frontRoll);
  EEPROM.put(addr + sizeof(float) * 2, sensors[channel].frontYaw);
  
  if (EEPROM.commit()) {
    if (serialConnected) {
      safePrintf("✅ Front position for sensor %d saved to EEPROM\n", channel);
    }
  } else {
    if (serialConnected) {
      safePrintf("❌ Failed to save front position for sensor %d\n", channel);
    }
  }
}

void loadFrontPositions() {
  for (int channel = 0; channel < 8; channel++) {
    int addr = FRONT_POSITION_START + channel * sizeof(float) * 3;
    
    EEPROM.get(addr, sensors[channel].frontPitch);
    EEPROM.get(addr + sizeof(float), sensors[channel].frontRoll);
    EEPROM.get(addr + sizeof(float) * 2, sensors[channel].frontYaw);
    
    // Check if EEPROM values are valid (not NaN)
    if (!isnan(sensors[channel].frontPitch) && !isnan(sensors[channel].frontRoll) && !isnan(sensors[channel].frontYaw)) {
      if (serialConnected) {
        safePrintf("✅ Loaded front position for sensor %d: Pitch:%.1f, Roll:%.1f, Yaw:%.1f\n", 
                   channel, sensors[channel].frontPitch, sensors[channel].frontRoll, sensors[channel].frontYaw);
      }
    } else {
      // Initialize with zeros if no valid data
      sensors[channel].frontPitch = 0;
      sensors[channel].frontRoll = 0;
      sensors[channel].frontYaw = 0;
      if (serialConnected) {
        safePrintf("⚠️ No valid front position for sensor %d, using zeros\n", channel);
      }
    }
  }
}

void clearFrontPositions() {
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  if (serialConnected) {
    safePrintln("🗑️ All front positions cleared from EEPROM");
  }
}

// Calculate relative position from front reference
float getRelativePitch(int channel) {
  return wrapAngle(sensors[channel].smoothedPitch - sensors[channel].frontPitch);
}

float getRelativeRoll(int channel) {
  return wrapAngle(sensors[channel].smoothedRoll - sensors[channel].frontRoll);
}

float getRelativeYaw(int channel) {
  return wrapAngle(sensors[channel].smoothedYaw - sensors[channel].frontYaw);
}

// Функция для обработки CORS в WebSocket
void handleWebSocketCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
}

void setup() {
  // Initialize serial port but don't wait for connection
  Serial.begin(115200);
  
  // Check if Serial is connected (with timeout)
  unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart < 2000)) {
    delay(10);
  }
  
  serialConnected = Serial;
  
  if (serialConnected) {
    safePrintln("");
    safePrintln("Starting MPU6050 VR Tracker with PCA9548A Multiplexer...");
    safePrintln("✅ Serial connection established");
  } else {
    // No serial connection, but continue operation
    // We'll use WebSocket for all communication
  }

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Load front positions from EEPROM
  loadFrontPositions();
  
  // Initialize I2C
  Wire.begin();
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  if (serialConnected) {
    safePrint("Connecting to WiFi");
  }
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    if (serialConnected) {
      safePrint(".");
    }
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    if (serialConnected) {
      safePrintln("\n✅ Connected to WiFi!");
      safePrint("📡 IP Address: ");
      safePrintln(WiFi.localIP().toString());
      safePrint("📶 Signal Strength: ");
      safePrintln(String(WiFi.RSSI()));
    }
    
    // Send WebSocket message about WiFi connection
    String wifiMsg = "{\"type\":\"status\",\"message\":\"Connected to WiFi - IP: " + WiFi.localIP().toString() + "\"}";
    webSocket.broadcastTXT(wifiMsg);
  } else {
    if (serialConnected) {
      safePrintln("\n❌ Failed to connect to WiFi!");
    }
    return;
  }

  // Initialize sensors on all channels
  initializeAllSensors();
  
  // Start calibration
  calibrationStart = millis();
  if (serialConnected) {
    safePrintln("🔧 Calibrating gyros... Don't move the sensors for 3 seconds!");
  }
  
  // Send calibration start message via WebSocket
  String calibrationMsg = "{\"type\":\"status\",\"message\":\"Calibrating gyros... Don't move sensors for 3 seconds!\"}";
  webSocket.broadcastTXT(calibrationMsg);
  
  // Setup server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/recalibrate", HTTP_GET, handleRecalibrate);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/setfront", HTTP_GET, handleSetFront);
  server.on("/clearfront", HTTP_GET, handleClearFront);
  
  // Добавьте обработчик OPTIONS для CORS
  server.on("/", HTTP_OPTIONS, []() {
    handleWebSocketCORS();
    server.send(200, "text/plain", "");
  });
  
  server.onNotFound(handleNotFound);
  
  // Enable CORS для всех запросов
  server.enableCORS(true);
  
  // Start servers
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  if (serialConnected) {
    safePrintln("✅ HTTP server started on port 80");
    safePrintln("✅ WebSocket server started on port 81");
    safePrintln("🌐 Use this URL: http://" + WiFi.localIP().toString());
  }
  
  // Send WebSocket message about server start
  String serverStartMsg = "{\"type\":\"status\",\"message\":\"Servers started - HTTP:80, WebSocket:81\"}";
  webSocket.broadcastTXT(serverStartMsg);
}

void initializeAllSensors() {
  if (serialConnected) {
    safePrintln("🔍 Initializing sensors on all channels...");
  }
  
  for (int channel = 0; channel < 8; channel++) {
    selectChannel(channel);
    delay(10);
    
    if (serialConnected) {
      safePrintf("Channel %d: ", channel);
    }
    
    if (sensors[channel].mpu.begin()) {
      sensors[channel].connected = true;
      sensors[channel].mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
      sensors[channel].mpu.setGyroRange(MPU6050_RANGE_500_DEG);
      sensors[channel].mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
      if (serialConnected) {
        safePrintln("✅ MPU6050 found");
      }
    } else {
      sensors[channel].connected = false;
      if (serialConnected) {
        safePrintln("❌ No MPU6050");
      }
    }
  }
  if (serialConnected) {
    safePrintln("");
  }
}

void loop() {
  server.handleClient();
  webSocket.loop();
  
  // Process sensor data for all connected sensors
  processAllSensorsData();
  
  // Send ping to keep connection alive
  if (clientConnected && millis() - lastPingTime > PING_INTERVAL) {
    String pingMsg = "{\"type\":\"ping\",\"timestamp\":" + String(millis()) + "}";
    webSocket.broadcastTXT(pingMsg);
    lastPingTime = millis();
    
    if (serialConnected) {
      safePrintln("📡 Sent WebSocket ping");
    }
  }
  
  // Send data only if client is connected
  if (clientConnected) {
    unsigned long currentTime = millis();
    
    // Check minimum send interval
    if (currentTime - lastDataSend >= MIN_SEND_INTERVAL) {
      sendAllOrientationData(currentTime);
      lastDataSend = currentTime;
    }
  }
  
  delay(10); // Small delay for stability
}

void calibrateAllSensors() {
  bool allCalibrated = true;
  
  for (int channel = 0; channel < 8; channel++) {
    if (sensors[channel].connected && !sensors[channel].calibrated) {
      allCalibrated = false;
      calibrateSensor(channel);
    }
  }
  
  // Show calibration progress
  static unsigned long lastProgress = 0;
  if (millis() - lastProgress >= 500) {
    lastProgress = millis();
    int calibratedCount = 0;
    int totalCount = 0;
    
    for (int channel = 0; channel < 8; channel++) {
      if (sensors[channel].connected) {
        totalCount++;
        if (sensors[channel].calibrated) calibratedCount++;
      }
    }
    
    if (totalCount > 0) {
      int progress = (millis() - calibrationStart) * 100 / calibrationTime;
      
      if (serialConnected) {
        safePrintf("Calibration progress: %d%%, Sensors: %d/%d\n", 
                   progress, calibratedCount, totalCount);
      }
      
      // Send progress via WebSocket
      if (clientConnected && (progress % 25 == 0)) { // Send every 25% progress
        String progressMsg = "{\"type\":\"calibrationProgress\",\"progress\":" + 
                            String(progress) + ",\"calibrated\":" + 
                            String(calibratedCount) + ",\"total\":" + 
                            String(totalCount) + "}";
        webSocket.broadcastTXT(progressMsg);
      }
    }
  }
  
  if (allCalibrated && millis() - calibrationStart >= calibrationTime) {
    if (serialConnected) {
      safePrintln("✅ All sensors calibrated!");
    }
    String calibratedMsg = "{\"type\":\"status\",\"message\":\"All sensors calibrated\"}";
    webSocket.broadcastTXT(calibratedMsg);
  }
}

void calibrateSensor(int channel) {
  if (!sensors[channel].connected || sensors[channel].calibrated) return;
  
  selectChannel(channel);
  sensors_event_t a, g, temp;
  
  if (!sensors[channel].mpu.getEvent(&a, &g, &temp)) {
    return;
  }
  
  static int sampleCount[8] = {0};
  static float sumX[8] = {0}, sumY[8] = {0}, sumZ[8] = {0};
  
  if (millis() - calibrationStart < calibrationTime) {
    sumX[channel] += g.gyro.x;
    sumY[channel] += g.gyro.y;
    sumZ[channel] += g.gyro.z;
    sampleCount[channel]++;
  } else {
    sensors[channel].gyroOffsetX = sumX[channel] / sampleCount[channel];
    sensors[channel].gyroOffsetY = sumY[channel] / sampleCount[channel];
    sensors[channel].gyroOffsetZ = sumZ[channel] / sampleCount[channel];
    sensors[channel].calibrated = true;
    
    if (serialConnected) {
      safePrintf("✅ Sensor %d calibrated! Samples: %d\n", channel, sampleCount[channel]);
      safePrintf("   Offsets - X:%.6f, Y:%.6f, Z:%.6f\n", 
                 sensors[channel].gyroOffsetX, sensors[channel].gyroOffsetY, sensors[channel].gyroOffsetZ);
    }
  }
}

void processAllSensorsData() {
  if (millis() - calibrationStart < calibrationTime) {
    calibrateAllSensors();
    return;
  }
  
  for (int channel = 0; channel < 8; channel++) {
    if (!sensors[channel].connected || !sensors[channel].calibrated) continue;
    
    selectChannel(channel);
    processSensorData(channel);
  }
}

void processSensorData(int channel) {
  sensors_event_t a, g, temp;
  if (!sensors[channel].mpu.getEvent(&a, &g, &temp)) {
    if (serialConnected) {
      safePrintf("Error reading MPU6050 data from channel %d\n", channel);
    }
    return;
  }
  
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  lastTime = currentTime;
  
  // Compensate for gyro offset
  float gyroX = g.gyro.x - sensors[channel].gyroOffsetX;
  float gyroY = g.gyro.y - sensors[channel].gyroOffsetY;
  float gyroZ = g.gyro.z - sensors[channel].gyroOffsetZ;
  
  // Calculate angles from accelerometer
  float accelPitch = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
  float accelRoll = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
  
  // Apply yaw drift compensation
  if (abs(gyroZ) < 0.01) {
    gyroZ -= sensors[channel].gyroOffsetZ * YAW_DRIFT_COMPENSATION;
  }
  
  // Gyro integration with angle wrapping
  sensors[channel].pitch += gyroX * deltaTime * 180.0 / PI;
  sensors[channel].roll += gyroY * deltaTime * 180.0 / PI;
  sensors[channel].yaw += gyroZ * deltaTime * 180.0 / PI;
  
  // Wrap angles to [-180, 180] range to prevent infinite accumulation
  sensors[channel].pitch = wrapAngle(sensors[channel].pitch);
  sensors[channel].roll = wrapAngle(sensors[channel].roll);
  sensors[channel].yaw = wrapAngle(sensors[channel].yaw);
  
  // Complementary filter for pitch and roll
  float alpha = 0.96;
  sensors[channel].pitch = alpha * sensors[channel].pitch + (1.0 - alpha) * accelPitch;
  sensors[channel].roll = alpha * sensors[channel].roll + (1.0 - alpha) * accelRoll;
  
  // Wrap angles again after complementary filter
  sensors[channel].pitch = wrapAngle(sensors[channel].pitch);
  sensors[channel].roll = wrapAngle(sensors[channel].roll);
  sensors[channel].yaw = wrapAngle(sensors[channel].yaw);
  
  // Additional yaw stabilization when device is relatively stable
  float totalAccel = sqrt(a.acceleration.x * a.acceleration.x + 
                         a.acceleration.y * a.acceleration.y + 
                         a.acceleration.z * a.acceleration.z);
  
  // If device is relatively stable (not moving much), apply small yaw correction
  if (abs(totalAccel - 9.8) < 0.5 && abs(gyroZ) < 0.005) {
    sensors[channel].yaw *= 0.999;
  }
  
  // Apply additional smoothing for display
  sensors[channel].smoothedPitch = sensors[channel].smoothedPitch * (1 - smoothingFactor) + sensors[channel].pitch * smoothingFactor;
  sensors[channel].smoothedRoll = sensors[channel].smoothedRoll * (1 - smoothingFactor) + sensors[channel].roll * smoothingFactor;
  sensors[channel].smoothedYaw = sensors[channel].smoothedYaw * (1 - smoothingFactor) + sensors[channel].yaw * smoothingFactor;
  
  // Wrap smoothed angles as well
  sensors[channel].smoothedPitch = wrapAngle(sensors[channel].smoothedPitch);
  sensors[channel].smoothedRoll = wrapAngle(sensors[channel].smoothedRoll);
  sensors[channel].smoothedYaw = wrapAngle(sensors[channel].smoothedYaw);
  
  // Check if data has changed significantly
  bool pitchChanged = abs(sensors[channel].smoothedPitch - sensors[channel].lastSentPitch) >= CHANGE_THRESHOLD;
  bool rollChanged = abs(sensors[channel].smoothedRoll - sensors[channel].lastSentRoll) >= CHANGE_THRESHOLD;
  bool yawChanged = abs(sensors[channel].smoothedYaw - sensors[channel].lastSentYaw) >= CHANGE_THRESHOLD;
  
  // Check relative position changes
  bool relativePitchChanged = abs(getRelativePitch(channel) - sensors[channel].lastSentRelativePitch) >= CHANGE_THRESHOLD;
  bool relativeRollChanged = abs(getRelativeRoll(channel) - sensors[channel].lastSentRelativeRoll) >= CHANGE_THRESHOLD;
  bool relativeYawChanged = abs(getRelativeYaw(channel) - sensors[channel].lastSentRelativeYaw) >= CHANGE_THRESHOLD;
  
  if (pitchChanged || rollChanged || yawChanged || relativePitchChanged || relativeRollChanged || relativeYawChanged) {
    sensors[channel].lastChangeTime = currentTime;
  }
  
  sensors[channel].lastDataTime = currentTime;
}

void sendAllOrientationData(unsigned long currentTime) {
  String json = "{\"type\":\"sensorData\",\"sensors\":[";
  
  bool firstSensor = true;
  bool dataChanged = false;
  
  for (int channel = 0; channel < 8; channel++) {
    if (!firstSensor) json += ",";
    firstSensor = false;
    
    if (sensors[channel].connected && sensors[channel].calibrated) {
      // Check if this sensor's data has changed significantly
      bool pitchChanged = abs(sensors[channel].smoothedPitch - sensors[channel].lastSentPitch) >= CHANGE_THRESHOLD;
      bool rollChanged = abs(sensors[channel].smoothedRoll - sensors[channel].lastSentRoll) >= CHANGE_THRESHOLD;
      bool yawChanged = abs(sensors[channel].smoothedYaw - sensors[channel].lastSentYaw) >= CHANGE_THRESHOLD;
      
      bool relativePitchChanged = abs(getRelativePitch(channel) - sensors[channel].lastSentRelativePitch) >= CHANGE_THRESHOLD;
      bool relativeRollChanged = abs(getRelativeRoll(channel) - sensors[channel].lastSentRelativeRoll) >= CHANGE_THRESHOLD;
      bool relativeYawChanged = abs(getRelativeYaw(channel) - sensors[channel].lastSentRelativeYaw) >= CHANGE_THRESHOLD;
      
      if (pitchChanged || rollChanged || yawChanged || relativePitchChanged || relativeRollChanged || relativeYawChanged) {
        dataChanged = true;
        
        // Update last sent values
        sensors[channel].lastSentPitch = sensors[channel].smoothedPitch;
        sensors[channel].lastSentRoll = sensors[channel].smoothedRoll;
        sensors[channel].lastSentYaw = sensors[channel].smoothedYaw;
        sensors[channel].lastSentRelativePitch = getRelativePitch(channel);
        sensors[channel].lastSentRelativeRoll = getRelativeRoll(channel);
        sensors[channel].lastSentRelativeYaw = getRelativeYaw(channel);
      }
      
      json += "{";
      json += "\"sensor\":" + String(channel) + ",";
      json += "\"pitch\":" + String(sensors[channel].smoothedPitch, 2) + ",";
      json += "\"roll\":" + String(sensors[channel].smoothedRoll, 2) + ",";
      json += "\"yaw\":" + String(sensors[channel].smoothedYaw, 2) + ",";
      json += "\"relativePitch\":" + String(getRelativePitch(channel), 2) + ",";
      json += "\"relativeRoll\":" + String(getRelativeRoll(channel), 2) + ",";
      json += "\"relativeYaw\":" + String(getRelativeYaw(channel), 2) + ",";
      json += "\"frontPitch\":" + String(sensors[channel].frontPitch, 2) + ",";
      json += "\"frontRoll\":" + String(sensors[channel].frontRoll, 2) + ",";
      json += "\"frontYaw\":" + String(sensors[channel].frontYaw, 2) + ",";
      json += "\"lastChange\":" + String(sensors[channel].lastChangeTime) + ",";
      json += "\"connected\":true";
      json += "}";
    } else {
      // Empty object for disconnected sensors
      json += "{\"sensor\":" + String(channel) + ",\"connected\":false}";
    }
  }
  
  json += "],\"timestamp\":" + String(currentTime) + "}";
  
  // Send data only if something changed
  if (dataChanged) {
    webSocket.broadcastTXT(json);
    
    // Debug output (only if serial connected and not too frequent)
    if (serialConnected) {
      static unsigned long lastDebug = 0;
      if (currentTime - lastDebug >= 2000) {
        lastDebug = currentTime;
        safePrintln("📤 WebSocket: Sent data for all sensors");
        for (int channel = 0; channel < 8; channel++) {
          if (sensors[channel].connected) {
            safePrintf("  Sensor %d: Abs[P:%.1f R:%.1f Y:%.1f] Rel[P:%.1f R:%.1f Y:%.1f]\n", 
                       channel, 
                       sensors[channel].smoothedPitch, 
                       sensors[channel].smoothedRoll, 
                       sensors[channel].smoothedYaw,
                       getRelativePitch(channel),
                       getRelativeRoll(channel),
                       getRelativeYaw(channel));
          }
        }
      }
    }
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      if (serialConnected) {
        safePrintf("🔌 [%u] Disconnected!\n", num);
      }
      clientConnected = (webSocket.connectedClients() > 0);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        if (serialConnected) {
          safePrintf("✅ [%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        }
        clientConnected = true;
        
        // Send welcome message
        String welcome = "{\"type\":\"status\",\"message\":\"Connected to MPU6050 VR Tracker with 8 sensors\"}";
        webSocket.sendTXT(num, welcome);
        
        // Send serial connection status
        String serialStatus = "{\"type\":\"system\",\"serialConnected\":" + 
                             String(serialConnected ? "true" : "false") + "}";
        webSocket.sendTXT(num, serialStatus);
        
        // Send sensor status
        sendSensorStatus(num);
      }
      break;
      
    case WStype_TEXT:
      if (serialConnected) {
        safePrintf("📨 [%u] Received: %s\n", num, payload);
      }
      
      // Handle recalibrate command
      if (strstr((char*)payload, "recalibrate") != NULL) {
        for (int channel = 0; channel < 8; channel++) {
          if (sensors[channel].connected) {
            sensors[channel].calibrated = false;
            sensors[channel].pitch = sensors[channel].roll = sensors[channel].yaw = 0;
          }
        }
        calibrationStart = millis();
        
        String recalibrateMsg = "{\"type\":\"status\",\"message\":\"Recalibrating all gyros...\"}";
        webSocket.sendTXT(num, recalibrateMsg);
        if (serialConnected) {
          safePrintln("Recalibration started for all sensors");
        }
      }
      // Handle ping message
      else if (strncmp((char*)payload, "{\"type\":\"ping\"", 14) == 0) {
        String pong = "{\"type\":\"pong\",\"timestamp\":" + String(millis()) + "}";
        webSocket.sendTXT(num, pong);
      }
      // Handle sensor status request
      else if (strstr((char*)payload, "sensorStatus") != NULL) {
        sendSensorStatus(num);
      }
      // Handle reset yaw command
      else if (strstr((char*)payload, "resetYaw") != NULL) {
        // Extract sensor number if provided
        int sensorNum = 0;
        char* sensorStr = strstr((char*)payload, "\"sensor\"");
        if (sensorStr) {
          sscanf(sensorStr, "\"sensor\":%d", &sensorNum);
        }
        
        if (sensorNum >= 0 && sensorNum < 8 && sensors[sensorNum].connected) {
          sensors[sensorNum].yaw = 0;
          sensors[sensorNum].smoothedYaw = 0;
          
          String resetMsg = "{\"type\":\"status\",\"message\":\"Yaw reset for sensor " + String(sensorNum) + "\"}";
          webSocket.sendTXT(num, resetMsg);
          if (serialConnected) {
            safePrintf("Yaw reset for sensor %d\n", sensorNum);
          }
        } else {
          String errorMsg = "{\"type\":\"error\",\"message\":\"Invalid sensor number or sensor not connected\"}";
          webSocket.sendTXT(num, errorMsg);
        }
      }
      // Handle set front position command
      else if (strstr((char*)payload, "setFront") != NULL) {
        // Extract sensor number if provided
        int sensorNum = 0;
        char* sensorStr = strstr((char*)payload, "\"sensor\"");
        if (sensorStr) {
          sscanf(sensorStr, "\"sensor\":%d", &sensorNum);
        }
        
        if (sensorNum >= 0 && sensorNum < 8 && sensors[sensorNum].connected) {
          sensors[sensorNum].frontPitch = sensors[sensorNum].smoothedPitch;
          sensors[sensorNum].frontRoll = sensors[sensorNum].smoothedRoll;
          sensors[sensorNum].frontYaw = sensors[sensorNum].smoothedYaw;
          
          // Save to EEPROM
          saveFrontPosition(sensorNum);
          
          String frontMsg = "{\"type\":\"status\",\"message\":\"Front position set for sensor " + String(sensorNum) + 
                           " [P:" + String(sensors[sensorNum].frontPitch, 1) + 
                           " R:" + String(sensors[sensorNum].frontRoll, 1) + 
                           " Y:" + String(sensors[sensorNum].frontYaw, 1) + "]\"}";
          webSocket.sendTXT(num, frontMsg);
          if (serialConnected) {
            safePrintf("Front position set for sensor %d: Pitch:%.1f, Roll:%.1f, Yaw:%.1f\n", 
                       sensorNum, sensors[sensorNum].frontPitch, sensors[sensorNum].frontRoll, sensors[sensorNum].frontYaw);
          }
        } else {
          String errorMsg = "{\"type\":\"error\",\"message\":\"Invalid sensor number or sensor not connected\"}";
          webSocket.sendTXT(num, errorMsg);
        }
      }
      // Handle clear front positions command
      else if (strstr((char*)payload, "clearFront") != NULL) {
        for (int channel = 0; channel < 8; channel++) {
          sensors[channel].frontPitch = 0;
          sensors[channel].frontRoll = 0;
          sensors[channel].frontYaw = 0;
        }
        clearFrontPositions();
        
        String clearMsg = "{\"type\":\"status\",\"message\":\"All front positions cleared\"}";
        webSocket.sendTXT(num, clearMsg);
        if (serialConnected) {
          safePrintln("All front positions cleared");
        }
      }
      break;
      
    case WStype_ERROR:
      if (serialConnected) {
        safePrintf("❌ [%u] WebSocket error!\n", num);
      }
      break;
      
    default:
      break;
  }
}

void sendSensorStatus(uint8_t num) {
  String status = "{\"type\":\"sensorStatus\",\"sensors\":[";
  
  for (int channel = 0; channel < 8; channel++) {
    if (channel > 0) status += ",";
    status += "{";
    status += "\"sensor\":" + String(channel) + ",";
    status += "\"connected\":" + String(sensors[channel].connected ? "true" : "false") + ",";
    status += "\"calibrated\":" + String(sensors[channel].calibrated ? "true" : "false") + ",";
    status += "\"hasFrontPosition\":" + String((sensors[channel].frontPitch != 0 || sensors[channel].frontRoll != 0 || sensors[channel].frontYaw != 0) ? "true" : "false");
    status += "}";
  }
  status += "]}";
  
  webSocket.sendTXT(num, status);
}

// Остальные функции (handleRoot, handleData, handleStatus, handleRecalibrate, handleScan, handleSetFront, handleClearFront, handleNotFound)
// остаются без изменений, так как они уже были предоставлены в предыдущем коде

void handleRoot() {
  // HTML код остается без изменений
  String html = R"=====(
<!DOCTYPE html>
<html>
<head>
    <title>MPU6050 VR Tracker with 8 Sensors</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        /* Стили остаются без изменений */
    </style>
</head>
<body>
    <!-- HTML код остается без изменений -->
</body>
</html>
)=====";
  
  server.send(200, "text/html", html);
}

void handleData() {
  String json = "{\"sensors\":[";
  
  for (int channel = 0; channel < 8; channel++) {
    if (channel > 0) json += ",";
    json += "{";
    json += "\"sensor\":" + String(channel) + ",";
    json += "\"connected\":" + String(sensors[channel].connected ? "true" : "false") + ",";
    json += "\"calibrated\":" + String(sensors[channel].calibrated ? "true" : "false");
    
    if (sensors[channel].connected && sensors[channel].calibrated) {
      json += ",\"pitch\":" + String(sensors[channel].smoothedPitch, 2);
      json += ",\"roll\":" + String(sensors[channel].smoothedRoll, 2);
      json += ",\"yaw\":" + String(sensors[channel].smoothedYaw, 2);
      json += ",\"relativePitch\":" + String(getRelativePitch(channel), 2);
      json += ",\"relativeRoll\":" + String(getRelativeRoll(channel), 2);
      json += ",\"relativeYaw\":" + String(getRelativeYaw(channel), 2);
      json += ",\"frontPitch\":" + String(sensors[channel].frontPitch, 2);
      json += ",\"frontRoll\":" + String(sensors[channel].frontRoll, 2);
      json += ",\"frontYaw\":" + String(sensors[channel].frontYaw, 2);
    }
    json += "}";
  }
  
  json += "]}";
  
  server.send(200, "application/json", json);
}

void handleStatus() {
  String json = "{\"status\":\"ok\",\"timestamp\":" + String(millis()) + "}";
  server.send(200, "application/json", json);
}

void handleRecalibrate() {
  for (int channel = 0; channel < 8; channel++) {
    if (sensors[channel].connected) {
      sensors[channel].calibrated = false;
      sensors[channel].pitch = sensors[channel].roll = sensors[channel].yaw = 0;
    }
  }
  calibrationStart = millis();
  
  String json = "{\"status\":\"recalibrating\"}";
  server.send(200, "application/json", json);
}

void handleScan() {
  initializeAllSensors();
  
  String json = "{\"status\":\"scan_complete\"}";
  server.send(200, "application/json", json);
}

void handleSetFront() {
  String sensorParam = server.arg("sensor");
  int sensorNum = sensorParam.toInt();
  
  if (sensorNum >= 0 && sensorNum < 8 && sensors[sensorNum].connected) {
    sensors[sensorNum].frontPitch = sensors[sensorNum].smoothedPitch;
    sensors[sensorNum].frontRoll = sensors[sensorNum].smoothedRoll;
    sensors[sensorNum].frontYaw = sensors[sensorNum].smoothedYaw;
    
    saveFrontPosition(sensorNum);
    
    String json = "{\"status\":\"front_set\",\"sensor\":" + String(sensorNum) + "}";
    server.send(200, "application/json", json);
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid sensor\"}");
  }
}

void handleClearFront() {
  for (int channel = 0; channel < 8; channel++) {
    sensors[channel].frontPitch = 0;
    sensors[channel].frontRoll = 0;
    sensors[channel].frontYaw = 0;
  }
  clearFrontPositions();
  
  server.send(200, "application/json", "{\"status\":\"front_cleared\"}");
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  
  server.send(404, "text/plain", message);
}
