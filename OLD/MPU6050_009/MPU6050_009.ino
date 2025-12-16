#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// Настройки WiFi сети
const char* ssid = "ESP8266_AP";
const char* password = "12345678";

// Создаем веб-сервер на порту 80
ESP8266WebServer server(80);
// WebSocket сервер на порту 81
WebSocketsServer webSocket = WebSocketsServer(81);

// Переменные для хранения состояния
int ledState = LOW;
unsigned long startTime = 0;
int visitorCount = 0;

// MPU6050 сенсор
Adafruit_MPU6050 mpu;
bool mpuConnected = false;

// Данные сенсора
float pitch = 0, roll = 0, yaw = 0;
float smoothedPitch = 0, smoothedRoll = 0, smoothedYaw = 0;
const float smoothingFactor = 0.3;

// Фильтр и калибровка
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
bool calibrated = false;
unsigned long lastTime = 0;
unsigned long calibrationStart = 0;
const unsigned long calibrationTime = 3000;

// Нулевая точка
float zeroPitch = 0, zeroRoll = 0, zeroYaw = 0;
bool zeroSet = false;

// EEPROM addresses for zero point storage
const int EEPROM_SIZE = 512;
const int ZERO_PITCH_ADDR = 0;
const int ZERO_ROLL_ADDR = sizeof(float);
const int ZERO_YAW_ADDR = sizeof(float) * 2;
const int ZERO_SET_ADDR = sizeof(float) * 3;

// Авто-калибровка
bool autoCalibrationEnabled = true;
const unsigned long AUTO_CALIBRATION_INTERVAL = 60000;
unsigned long lastAutoCalibration = 0;

// Управление отправкой данных
unsigned long lastDataSend = 0;
const unsigned long DATA_SEND_INTERVAL = 50;

// Функция для форматирования времени
String formatTime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  
  char buffer[50];
  if (days > 0) {
    snprintf(buffer, sizeof(buffer), "%luд %02lu:%02lu:%02lu", days, hours, minutes, seconds);
  } else {
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes, seconds);
  }
  return String(buffer);
}

// Функция для получения силы сигнала WiFi
String getWiFiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    return "Подключено";
  } else {
    return "Не подключено";
  }
}

String getWiFiRSSI() {
  if (WiFi.status() == WL_CONNECTED) {
    return String(WiFi.RSSI());
  } else {
    return "N/A";
  }
}

// Инициализация MPU6050
bool initializeMPU6050() {
  Serial.println("🔍 Инициализация MPU6050...");
  
  if (mpu.begin()) {
    // Настройка MPU6050
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setGyroRange(MPU6050_RANGE_250_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
    
    Serial.println("✅ MPU6050 найден и инициализирован");
    return true;
  } else {
    Serial.println("❌ MPU6050 не найден!");
    return false;
  }
}

// Load zero point from EEPROM
void loadZeroPoint() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(ZERO_PITCH_ADDR, zeroPitch);
  EEPROM.get(ZERO_ROLL_ADDR, zeroRoll);
  EEPROM.get(ZERO_YAW_ADDR, zeroYaw);
  
  byte zeroFlag;
  EEPROM.get(ZERO_SET_ADDR, zeroFlag);
  zeroSet = (zeroFlag == 1);
  
  if (zeroSet) {
    Serial.printf("📁 Loaded zero point - Pitch:%.1f° Roll:%.1f° Yaw:%.1f°\n", 
                 zeroPitch, zeroRoll, zeroYaw);
  }
}

// Save zero point to EEPROM
void saveZeroPoint() {
  zeroPitch = smoothedPitch;
  zeroRoll = smoothedRoll;
  zeroYaw = smoothedYaw;
  zeroSet = true;
  
  EEPROM.put(ZERO_PITCH_ADDR, zeroPitch);
  EEPROM.put(ZERO_ROLL_ADDR, zeroRoll);
  EEPROM.put(ZERO_YAW_ADDR, zeroYaw);
  EEPROM.put(ZERO_SET_ADDR, (byte)1);
  EEPROM.commit();
  
  Serial.printf("💾 Zero point saved - Pitch:%.1f° Roll:%.1f° Yaw:%.1f°\n", 
               zeroPitch, zeroRoll, zeroYaw);
}

// Reset zero point
void resetZeroPoint() {
  zeroPitch = 0;
  zeroRoll = 0;
  zeroYaw = 0;
  zeroSet = false;
  
  EEPROM.put(ZERO_SET_ADDR, (byte)0);
  EEPROM.commit();
  
  Serial.println("🔄 Zero point reset");
}

// Обработка данных сенсора
void processSensorData() {
  if (!calibrated) {
    calibrateGyro();
    return;
  }
  
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    Serial.println("Ошибка чтения данных MPU6050");
    return;
  }
  
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  if (lastTime == 0) {
    deltaTime = 0.01;
  }
  lastTime = currentTime;
  
  // Компенсация смещения гироскопа
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  // ТОЛЬКО интеграция гироскопа (без ограничений)
  pitch += gyroX * deltaTime * 180.0 / PI;
  roll += gyroY * deltaTime * 180.0 / PI;
  yaw += gyroZ * deltaTime * 180.0 / PI;
  
  // Сглаживание для отображения
  smoothedPitch = smoothedPitch * (1 - smoothingFactor) + pitch * smoothingFactor;
  smoothedRoll = smoothedRoll * (1 - smoothingFactor) + roll * smoothingFactor;
  smoothedYaw = smoothedYaw * (1 - smoothingFactor) + yaw * smoothingFactor;
  
  // Отладочный вывод для отслеживания дрейфа
  static unsigned long lastDebug = 0;
  if (currentTime - lastDebug > 10000) { // Каждые 10 секунд
    lastDebug = currentTime;
    Serial.printf("📊 Текущие углы - Pitch: %.1f°, Roll: %.1f°, Yaw: %.1f°\n", pitch, roll, yaw);
  }
}

// Калибровка гироскопа
void calibrateGyro() {
  if (calibrated) return;
  
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  static int sampleCount = 0;
  static float sumX = 0, sumY = 0, sumZ = 0;
  
  if (millis() - calibrationStart < calibrationTime) {
    sumX += g.gyro.x;
    sumY += g.gyro.y;
    sumZ += g.gyro.z;
    sampleCount++;
    
    if (sampleCount % 50 == 0) {
      int progress = (millis() - calibrationStart) * 100 / calibrationTime;
      float currentOffsetX = sumX / sampleCount;
      float currentOffsetY = sumY / sampleCount;
      float currentOffsetZ = sumZ / sampleCount;
      Serial.printf("Калибровка: %d%%, Offsets: X:%.6f, Y:%.6f, Z:%.6f\n", 
                   progress, currentOffsetX, currentOffsetY, currentOffsetZ);
    }
  } else {
    gyroOffsetX = sumX / sampleCount;
    gyroOffsetY = sumY / sampleCount;
    gyroOffsetZ = sumZ / sampleCount;
    calibrated = true;
    
    // Сброс углов после калибровки
    pitch = 0;
    roll = 0;
    yaw = 0;
    smoothedPitch = 0;
    smoothedRoll = 0;
    smoothedYaw = 0;
    
    Serial.println("✅ Калибровка гироскопа завершена!");
    Serial.printf("Финальные смещения - X:%.6f, Y:%.6f, Z:%.6f\n", gyroOffsetX, gyroOffsetY, gyroOffsetZ);
    Serial.printf("Обработано samples: %d\n", sampleCount);
  }
}

// Функция для принудительного сброса углов
void resetAllAngles() {
  pitch = 0;
  roll = 0;
  yaw = 0;
  smoothedPitch = 0;
  smoothedRoll = 0;
  smoothedYaw = 0;
  Serial.println("🔄 Все углы сброшены в 0");
}

// Отправка данных сенсора через WebSocket
void sendSensorData() {
  if (webSocket.connectedClients() == 0) return;
  
  // Расчет относительных углов (теперь без нормализации)
  float relPitch = calculateRelativeAngle(smoothedPitch, zeroPitch);
  float relRoll = calculateRelativeAngle(smoothedRoll, zeroRoll);
  float relYaw = calculateRelativeAngle(smoothedYaw, zeroYaw);
  
  // Создание JSON данных
  String json = "{";
  json += "\"type\":\"sensorData\",";
  json += "\"pitch\":" + String(smoothedPitch, 2) + ",";
  json += "\"roll\":" + String(smoothedRoll, 2) + ",";
  json += "\"yaw\":" + String(smoothedYaw, 2) + ",";
  json += "\"relPitch\":" + String(relPitch, 2) + ",";
  json += "\"relRoll\":" + String(relRoll, 2) + ",";
  json += "\"relYaw\":" + String(relYaw, 2) + ",";
  json += "\"zeroSet\":" + String(zeroSet ? "true" : "false") + ",";
  json += "\"calibrated\":" + String(calibrated ? "true" : "false") + ",";
  json += "\"autoCalibration\":" + String(autoCalibrationEnabled ? "true" : "false") + ",";
  json += "\"signal\":" + String(WiFi.RSSI()) + ",";
  json += "\"timestamp\":" + String(millis());
  json += "}";
  
  // Отправка всем подключенным клиентам
  webSocket.broadcastTXT(json);
}

// Расчет относительного угла
float calculateRelativeAngle(float absoluteAngle, float zeroAngle) {
  float relative = absoluteAngle - zeroAngle;
  return relative;
}

// Установка нулевой точки
void setZeroPoint() {
  saveZeroPoint();
  
  Serial.printf("💾 Нулевая точка установлена - Pitch:%.1f° Roll:%.1f° Yaw:%.1f°\n", 
               zeroPitch, zeroRoll, zeroYaw);
}

// Сброс Yaw
void resetYaw() {
  yaw = 0;
  smoothedYaw = 0;
  
  Serial.println("🔄 Yaw сброшен");
}

// Перекалибровка
void recalibrate() {
  calibrated = false;
  pitch = roll = yaw = 0;
  calibrationStart = millis();
  
  Serial.println("🔄 Перекалибровка запущена");
}

// Установка авто-калибровки
void setAutoCalibration(bool enable) {
  autoCalibrationEnabled = enable;
  if (enable) {
    lastAutoCalibration = millis();
  }
  
  Serial.printf("⚙️ Авто-калибровка %s\n", enable ? "включена" : "выключена");
}

// Обработчик WebSocket событий
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("🔌 [%u] Отключен!\n", num);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("✅ [%u] Подключен от %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        
        // Отправка статуса калибровки
        String calStatus = "{\"type\":\"calibrationStatus\",\"calibrated\":" + String(calibrated ? "true" : "false") + "}";
        webSocket.sendTXT(num, calStatus);
        
        // Отправка статуса авто-калибровки
        String autoCalStatus = "{\"type\":\"autoCalibrationStatus\",\"enabled\":" + String(autoCalibrationEnabled ? "true" : "false") + "}";
        webSocket.sendTXT(num, autoCalStatus);
        
        if (zeroSet) {
          String zeroInfo = "{\"type\":\"zeroInfo\",\"zeroPitch\":" + String(zeroPitch, 2) + 
                           ",\"zeroRoll\":" + String(zeroRoll, 2) + 
                           ",\"zeroYaw\":" + String(zeroYaw, 2) + "}";
          webSocket.sendTXT(num, zeroInfo);
        }
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("📨 [%u] Получено: %s\n", num, payload);
        
        DynamicJsonDocument doc(256);
        deserializeJson(doc, message);
        String command = doc["type"];
        
        if (command == "ledOn") {
          ledState = HIGH;
          digitalWrite(LED_BUILTIN, ledState);
          String response = "{\"type\":\"status\",\"message\":\"LED включен\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "ledOff") {
          ledState = LOW;
          digitalWrite(LED_BUILTIN, ledState);
          String response = "{\"type\":\"status\",\"message\":\"LED выключен\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "blink") {
          String response = "{\"type\":\"status\",\"message\":\"LED мигает\"}";
          webSocket.sendTXT(num, response);
          for(int i = 0; i < 10; i++) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(500);
          }
          digitalWrite(LED_BUILTIN, ledState);
        }
        else if (command == "setZero") {
          setZeroPoint();
          String zeroInfo = "{\"type\":\"zeroInfo\",\"zeroPitch\":" + String(zeroPitch, 2) + 
                           ",\"zeroRoll\":" + String(zeroRoll, 2) + 
                           ",\"zeroYaw\":" + String(zeroYaw, 2) + "}";
          webSocket.broadcastTXT(zeroInfo);
        }
        else if (command == "resetZero") {
          resetZeroPoint();
          String zeroReset = "{\"type\":\"zeroReset\"}";
          webSocket.broadcastTXT(zeroReset);
        }
        else if (command == "resetYaw") {
          resetYaw();
          String response = "{\"type\":\"status\",\"message\":\"Yaw сброшен\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "recalibrate") {
          recalibrate();
        }
        else if (command == "setAutoCalibration") {
          bool enable = doc["enable"];
          setAutoCalibration(enable);
          String response = "{\"type\":\"status\",\"message\":\"Авто-калибровка " + String(enable ? "включена" : "выключена") + "\"}";
          webSocket.sendTXT(num, response);
          String autoCalUpdate = "{\"type\":\"autoCalibrationUpdate\",\"enabled\":" + String(enable ? "true" : "false") + "}";
          webSocket.broadcastTXT(autoCalUpdate);
        }
        else if (command == "restart") {
          String response = "{\"type\":\"status\",\"message\":\"Перезагрузка...\"}";
          webSocket.sendTXT(num, response);
          delay(1000);
          ESP.restart();
        }
        else if (command == "resetAngles") {
          resetAllAngles();
          String response = "{\"type\":\"status\",\"message\":\"Все углы сброшены\"}";
          webSocket.sendTXT(num, response);
        }          
      }
      break;
  }
}

// Функция для обработки главной страницы
void handleRoot() {
  visitorCount++;
  
  WiFiClient client = server.client();
  // Быстрая отправка заголовков
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println("Access-Control-Allow-Origin: *");
  client.println();
  
  // Отправка HTML построчно
  client.println("<!DOCTYPE html>");
  client.println("<html>");
  client.println("<head>");
  client.println("<meta charset='UTF-8'>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  client.println("<title>ESP8266 MPU6050 VR Head Tracker</title>");
  client.println("<style>");
  client.println("body { ");
  client.println("  font-family: Arial, sans-serif; ");
  client.println("  margin: 0;");
  client.println("  padding: 20px;");
  client.println("  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);");
  client.println("  min-height: 100vh;");
  client.println("  color: #333;");
  client.println("}");
  client.println(".container { ");
  client.println("  max-width: 1200px; ");
  client.println("  margin: 0 auto; ");
  client.println("  background: rgba(255,255,255,0.95); ");
  client.println("  padding: 20px; ");
  client.println("  border-radius: 15px; ");
  client.println("  box-shadow: 0 8px 32px rgba(0,0,0,0.1);");
  client.println("}");
  client.println(".header { ");
  client.println("  text-align: center; ");
  client.println("  margin-bottom: 30px;");
  client.println("  background: linear-gradient(135deg, #4CAF50, #45a049);");
  client.println("  color: white;");
  client.println("  padding: 20px;");
  client.println("  border-radius: 10px;");
  client.println("}");
  client.println(".dashboard {");
  client.println("  display: grid;");
  client.println("  grid-template-columns: 1fr 1fr;");
  client.println("  gap: 20px;");
  client.println("  margin-bottom: 20px;");
  client.println("}");
  client.println(".panel {");
  client.println("  background: #f8f9fa;");
  client.println("  padding: 20px;");
  client.println("  border-radius: 10px;");
  client.println("  border-left: 4px solid #4CAF50;");
  client.println("}");
  client.println(".visualization-panel {");
  client.println("  grid-column: 1 / -1;");
  client.println("  background: #2c3e50;");
  client.println("  color: white;");
  client.println("  padding: 20px;");
  client.println("  border-radius: 10px;");
  client.println("}");
  client.println(".controls {");
  client.println("  display: grid;");
  client.println("  grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));");
  client.println("  gap: 10px;");
  client.println("  margin: 20px 0;");
  client.println("}");
  client.println(".btn {");
  client.println("  padding: 12px 15px;");
  client.println("  border: none;");
  client.println("  border-radius: 5px;");
  client.println("  cursor: pointer;");
  client.println("  font-size: 14px;");
  client.println("  font-weight: bold;");
  client.println("  transition: all 0.3s;");
  client.println("}");
  client.println(".btn-primary { background: #4CAF50; color: white; }");
  client.println(".btn-warning { background: #ff9800; color: white; }");
  client.println(".btn-danger { background: #f44336; color: white; }");
  client.println(".btn-info { background: #2196F3; color: white; }");
  client.println(".btn-secondary { background: #6c757d; color: white; }");
  client.println(".btn:hover { transform: translateY(-2px); box-shadow: 0 4px 8px rgba(0,0,0,0.2); }");
  client.println(".data-grid {");
  client.println("  display: grid;");
  client.println("  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));");
  client.println("  gap: 15px;");
  client.println("  margin: 20px 0;");
  client.println("}");
  client.println(".data-card {");
  client.println("  background: white;");
  client.println("  padding: 15px;");
  client.println("  border-radius: 8px;");
  client.println("  box-shadow: 0 2px 4px rgba(0,0,0,0.1);");
  client.println("  text-align: center;");
  client.println("}");
  client.println(".data-value {");
  client.println("  font-size: 24px;");
  client.println("  font-weight: bold;");
  client.println("  margin: 10px 0;");
  client.println("}");
  client.println(".data-label {");
  client.println("  font-size: 12px;");
  client.println("  color: #666;");
  client.println("  text-transform: uppercase;");
  client.println("}");
  client.println(".direction-positive { color: #4CAF50; }");
  client.println(".direction-negative { color: #f44336; }");
  client.println(".direction-zero { color: #ff9800; }");
  client.println(".cube-container {");
  client.println("  width: 300px;");
  client.println("  height: 300px;");
  client.println("  margin: 20px auto;");
  client.println("  perspective: 1000px;");
  client.println("}");
  client.println(".cube {");
  client.println("  width: 100%;");
  client.println("  height: 100%;");
  client.println("  position: relative;");
  client.println("  transform-style: preserve-3d;");
  client.println("  transition: transform 0.1s ease-out;");
  client.println("}");
  client.println(".face {");
  client.println("  position: absolute;");
  client.println("  width: 300px;");
  client.println("  height: 300px;");
  client.println("  border: 3px solid #34495e;");
  client.println("  display: flex;");
  client.println("  align-items: center;");
  client.println("  justify-content: center;");
  client.println("  font-size: 24px;");
  client.println("  font-weight: bold;");
  client.println("  color: white;");
  client.println("  background: rgba(52, 152, 219, 0.8);");
  client.println("}");
  client.println(".front  { transform: rotateY(0deg) translateZ(150px); background: rgba(231, 76, 60, 0.8); }");
  client.println(".back   { transform: rotateY(180deg) translateZ(150px); background: rgba(52, 152, 219, 0.8); }");
  client.println(".right  { transform: rotateY(90deg) translateZ(150px); background: rgba(46, 204, 113, 0.8); }");
  client.println(".left   { transform: rotateY(-90deg) translateZ(150px); background: rgba(155, 89, 182, 0.8); }");
  client.println(".top    { transform: rotateX(90deg) translateZ(150px); background: rgba(241, 196, 15, 0.8); }");
  client.println(".bottom { transform: rotateX(-90deg) translateZ(150px); background: rgba(230, 126, 34, 0.8); }");
  client.println(".wifi-status { ");
  client.println("  padding: 8px; ");
  client.println("  margin: 5px 0; ");
  client.println("  border-radius: 5px;");
  client.println("  font-size: 14px;");
  client.println("}");
  client.println(".connected { background: #d4edda; color: #155724; }");
  client.println(".disconnected { background: #f8d7da; color: #721c24; }");
  client.println(".websocket-status {");
  client.println("  padding: 10px;");
  client.println("  border-radius: 5px;");
  client.println("  margin: 10px 0;");
  client.println("  text-align: center;");
  client.println("  font-weight: bold;");
  client.println("}");
  client.println(".ws-connected { background: #d4edda; color: #155724; }");
  client.println(".ws-disconnected { background: #f8d7da; color: #721c24; }");
  client.println(".info-section {");
  client.println("  background: #e8f5e8;");
  client.println("  padding: 15px;");
  client.println("  border-radius: 8px;");
  client.println("  margin: 15px 0;");
  client.println("  border-left: 4px solid #4CAF50;");
  client.println("}");
  client.println("@media (max-width: 768px) {");
  client.println("  .dashboard {");
  client.println("    grid-template-columns: 1fr;");
  client.println("  }");
  client.println("  .cube-container {");
  client.println("    width: 200px;");
  client.println("    height: 200px;");
  client.println("  }");
  client.println("  .face {");
  client.println("    width: 200px;");
  client.println("    height: 200px;");
  client.println("    font-size: 18px;");
  client.println("  }");
  client.println("  .front  { transform: rotateY(0deg) translateZ(100px); }");
  client.println("  .back   { transform: rotateY(180deg) translateZ(100px); }");
  client.println("  .right  { transform: rotateY(90deg) translateZ(100px); }");
  client.println("  .left   { transform: rotateY(-90deg) translateZ(100px); }");
  client.println("  .top    { transform: rotateX(90deg) translateZ(100px); }");
  client.println("  .bottom { transform: rotateX(-90deg) translateZ(100px); }");
  client.println("}");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  client.println("  <div class='container'>");
  client.println("    <div class='header'>");
  client.println("      <h1>🎮 MPU6050 VR Head Tracker</h1>");
  client.println("      <p>Real-time orientation tracking with 3D visualization</p>");
  client.println("    </div>");
  
  String wifiClass = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
  String wifiStatus = getWiFiStatus() + " | " + WiFi.SSID();
  client.println("    <div class='wifi-status " + wifiClass + "'>");
  client.println("      WiFi: " + wifiStatus);
  client.println("    </div>");

  client.println("    <div class='websocket-status' id='wsStatus'>");
  client.println("      WebSocket: Connecting...");
  client.println("    </div>");
  
  client.println("    <div class='dashboard'>");
  client.println("      <div class='panel'>");
  client.println("        <h3>📊 Absolute Orientation</h3>");
  client.println("        <div class='data-grid'>");
  client.println("          <div class='data-card'>");
  client.println("            <div class='data-label'>Pitch (X)</div>");
  client.println("            <div class='data-value' id='absPitch'>0.00°</div>");
  client.println("            <div class='data-label'>Front-Back Tilt</div>");
  client.println("          </div>");
  client.println("          <div class='data-card'>");
  client.println("            <div class='data-label'>Roll (Y)</div>");
  client.println("            <div class='data-value' id='absRoll'>0.00°</div>");
  client.println("            <div class='data-label'>Left-Right Tilt</div>");
  client.println("          </div>");
  client.println("          <div class='data-card'>");
  client.println("            <div class='data-label'>Yaw (Z)</div>");
  client.println("            <div class='data-value' id='absYaw'>0.00°</div>");
  client.println("            <div class='data-label'>Head Rotation</div>");
  client.println("          </div>");
  client.println("        </div>");
  client.println("      </div>");

  client.println("      <div class='panel'>");
  client.println("        <h3>🎯 Relative to Zero</h3>");
  client.println("        <div class='data-grid'>");
  client.println("          <div class='data-card'>");
  client.println("            <div class='data-label'>Pitch</div>");
  client.println("            <div class='data-value'>");
  client.println("              <span id='relPitch'>0.00°</span> <span id='dirPitch' class='direction-zero'>●</span>");
  client.println("            </div>");
  client.println("          </div>");
  client.println("          <div class='data-card'>");
  client.println("            <div class='data-label'>Roll</div>");
  client.println("            <div class='data-value'>");
  client.println("              <span id='relRoll'>0.00°</span> <span id='dirRoll' class='direction-zero'>●</span>");
  client.println("            </div>");
  client.println("          </div>");
  client.println("          <div class='data-card'>");
  client.println("            <div class='data-label'>Yaw</div>");
  client.println("            <div class='data-value'>");
  client.println("              <span id='relYaw'>0.00°</span> <span id='dirYaw' class='direction-zero'>●</span>");
  client.println("            </div>");
  client.println("          </div>");
  client.println("        </div>");
  client.println("      </div>");

  client.println("      <div class='visualization-panel'>");
  client.println("        <h3>🎮 3D Head Orientation</h3>");
  client.println("        <div class='cube-container'>");
  client.println("          <div class='cube' id='cube'>");
  client.println("            <div class='face front'>FACE</div>");
  client.println("            <div class='face back'>BACK</div>");
  client.println("            <div class='face right'>RIGHT</div>");
  client.println("            <div class='face left'>LEFT</div>");
  client.println("            <div class='face top'>TOP</div>");
  client.println("            <div class='face bottom'>BOTTOM</div>");
  client.println("          </div>");
  client.println("        </div>");
  client.println("      </div>");
  client.println("    </div>");

  client.println("    <div class='controls'>");
  client.println("      <button class='btn btn-primary' onclick=\"sendCommand('setZero')\">");
  client.println("        🎯 Set Zero Point");
  client.println("      </button>");
  client.println("      <button class='btn btn-warning' onclick=\"sendCommand('resetZero')\">");
  client.println("        🔄 Reset Zero");
  client.println("      </button>");
  client.println("      <button class='btn btn-info' onclick=\"sendCommand('recalibrate')\">");
  client.println("        🔧 Recalibrate");
  client.println("      </button>");
  client.println("      <button class='btn btn-danger' onclick=\"sendCommand('resetYaw')\">");
  client.println("        🎯 Reset Yaw");
  client.println("      </button>");
  client.println("      <button class='btn btn-info' onclick=\"sendCommand('resetAngles')\">");
  client.println("        📊 Reset All Angles");
  client.println("      </button>");
  client.println("      <button class='btn btn-secondary' onclick=\"sendCommand('ledOn')\">");
  client.println("        💡 LED On");
  client.println("      </button>");
  client.println("      <button class='btn btn-secondary' onclick=\"sendCommand('ledOff')\">");
  client.println("        💡 LED Off");
  client.println("      </button>");
  client.println("    </div>");

  client.println("    <div class='info-section'>");
  client.println("      <h4>📈 System Status</h4>");
  client.println("      <div class='data-grid'>");
  client.println("        <div class='data-card'>");
  client.println("          <div class='data-label'>Device State</div>");
  client.println("          <div class='data-value' id='deviceState'>-</div>");
  client.println("        </div>");
  client.println("        <div class='data-card'>");
  client.println("          <div class='data-label'>WiFi Signal</div>");
  client.println("          <div class='data-value' id='wifiSignal'>-</div>");
  client.println("        </div>");
  client.println("        <div class='data-card'>");
  client.println("          <div class='data-label'>Zero Point</div>");
  client.println("          <div class='data-value' id='zeroStatus'>-</div>");
  client.println("        </div>");
  client.println("        <div class='data-card'>");
  client.println("          <div class='data-label'>Calibration</div>");
  client.println("          <div class='data-value' id='calibrationStatus'>-</div>");
  client.println("        </div>");
  client.println("      </div>");
  client.println("    </div>");

  String ledClass = ledState ? "btn-primary" : "btn-secondary";
  String ledText = ledState ? "ВКЛЮЧЕН" : "ВЫКЛЮЧЕН";
  client.println("    <div class='info-section'>");
  client.println("      <h4>💡 System Information</h4>");
  client.println("      <p><strong>Время работы:</strong> " + formatTime(millis() - startTime) + "</p>");
  client.println("      <p><strong>Посетителей:</strong> " + String(visitorCount) + "</p>");
  client.println("      <p><strong>Статус LED:</strong> " + ledText + "</p>");
  client.println("      <p><strong>IP адрес:</strong> " + WiFi.localIP().toString() + "</p>");
  client.println("      <p><strong>Сила сигнала:</strong> " + getWiFiRSSI() + " dBm</p>");
  client.println("    </div>");
  client.println("  </div>");

  client.println("  <script>");
  client.println("    let ws = null;");
  client.println("    let cube = document.getElementById('cube');");
  client.println("    let connectionStatus = document.getElementById('wsStatus');");
  client.println("    ");
  client.println("    // Orientation data history for smoothing");
  client.println("    let orientationHistory = {");
  client.println("      pitch: [],");
  client.println("      roll: [],");
  client.println("      yaw: []");
  client.println("    };");
  client.println("    ");
  client.println("    function connectWebSocket() {");
  client.println("      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';");
  client.println("      const wsUrl = `${protocol}//${window.location.hostname}:81`;");
  client.println("      ");
  client.println("      connectionStatus.textContent = '🟡 Connecting...';");
  client.println("      connectionStatus.className = 'websocket-status ws-disconnected';");
  client.println("      ");
  client.println("      try {");
  client.println("        ws = new WebSocket(wsUrl);");
  client.println("        ");
  client.println("        ws.onopen = function() {");
  client.println("          console.log('✅ WebSocket connected');");
  client.println("          connectionStatus.textContent = '🟢 WebSocket: Connected';");
  client.println("          connectionStatus.className = 'websocket-status ws-connected';");
  client.println("        };");
  client.println("        ");
  client.println("        ws.onmessage = function(event) {");
  client.println("          try {");
  client.println("            const data = JSON.parse(event.data);");
  client.println("            handleWebSocketMessage(data);");
  client.println("          } catch (e) {");
  client.println("            console.error('Error parsing WebSocket message:', e);");
  client.println("          }");
  client.println("        };");
  client.println("        ");
  client.println("        ws.onclose = function(event) {");
  client.println("          console.log('❌ WebSocket disconnected');");
  client.println("          connectionStatus.textContent = '🔴 WebSocket: Disconnected';");
  client.println("          connectionStatus.className = 'websocket-status ws-disconnected';");
  client.println("          setTimeout(connectWebSocket, 3000);");
  client.println("        };");
  client.println("        ");
  client.println("        ws.onerror = function(error) {");
  client.println("          console.error('WebSocket error:', error);");
  client.println("        };");
  client.println("        ");
  client.println("      } catch (error) {");
  client.println("        console.error('Failed to create WebSocket:', error);");
  client.println("      }");
  client.println("    }");
  client.println("    ");
  client.println("    function handleWebSocketMessage(data) {");
  client.println("      if (data.type === 'sensorData') {");
  client.println("        updateDashboard(data);");
  client.println("        update3DVisualization(data);");
  client.println("        updateSystemStatus(data);");
  client.println("      } else if (data.type === 'status') {");
  client.println("        console.log('System message:', data.message);");
  client.println("        showNotification(data.message, 'info');");
  client.println("      } else if (data.type === 'zeroInfo') {");
  client.println("        console.log('Zero point updated:', data);");
  client.println("        showNotification('Zero point set successfully', 'success');");
  client.println("        document.getElementById('zeroStatus').textContent = 'Set';");
  client.println("        document.getElementById('zeroStatus').style.color = '#4CAF50';");
  client.println("      } else if (data.type === 'zeroReset') {");
  client.println("        console.log('Zero point reset');");
  client.println("        showNotification('Zero point reset', 'info');");
  client.println("        document.getElementById('zeroStatus').textContent = 'Not Set';");
  client.println("        document.getElementById('zeroStatus').style.color = '#f44336';");
  client.println("      } else if (data.type === 'calibrationStatus') {");
  client.println("        document.getElementById('calibrationStatus').textContent = data.calibrated ? 'Calibrated' : 'Calibrating...';");
  client.println("        document.getElementById('calibrationStatus').style.color = data.calibrated ? '#4CAF50' : '#ff9800';");
  client.println("      }");
  client.println("    }");
  client.println("    ");
  client.println("    function updateDashboard(data) {");
  client.println("      // Update absolute orientation");
  client.println("      document.getElementById('absPitch').textContent = data.pitch.toFixed(1) + '°';");
  client.println("      document.getElementById('absRoll').textContent = data.roll.toFixed(1) + '°';");
  client.println("      document.getElementById('absYaw').textContent = data.yaw.toFixed(1) + '°';");
  client.println("      ");
  client.println("      // Update relative orientation");
  client.println("      document.getElementById('relPitch').textContent = data.relPitch.toFixed(1) + '°';");
  client.println("      document.getElementById('relRoll').textContent = data.relRoll.toFixed(1) + '°';");
  client.println("      document.getElementById('relYaw').textContent = data.relYaw.toFixed(1) + '°';");
  client.println("      ");
  client.println("      // Update direction indicators");
  client.println("      updateDirectionIndicator('dirPitch', data.relPitch);");
  client.println("      updateDirectionIndicator('dirRoll', data.relRoll);");
  client.println("      updateDirectionIndicator('dirYaw', data.relYaw);");
  client.println("    }");
  client.println("    ");
  client.println("    function updateDirectionIndicator(elementId, value) {");
  client.println("      const element = document.getElementById(elementId);");
  client.println("      if (value > 1) {");
  client.println("        element.textContent = '↗';");
  client.println("        element.className = 'direction-positive';");
  client.println("      } else if (value < -1) {");
  client.println("        element.textContent = '↙';");
  client.println("        element.className = 'direction-negative';");
  client.println("      } else {");
  client.println("        element.textContent = '●';");
  client.println("        element.className = 'direction-zero';");
  client.println("      }");
  client.println("    }");
  client.println("    ");
  client.println("    function update3DVisualization(data) {");
  client.println("      // Apply smooth rotation to the cube");
  client.println("      const smoothPitch = smoothValue('pitch', data.relPitch);");
  client.println("      const smoothRoll = smoothValue('roll', data.relRoll);");
  client.println("      const smoothYaw = smoothValue('yaw', data.relYaw);");
  client.println("      ");
  client.println("      // Correct rotation logic for VR head tracking:");
  client.println("      // - Pitch rotates around X axis (forward/backward tilt)");
  client.println("      // - Yaw rotates around Y axis (head rotation left/right)");
  client.println("      // - Roll rotates around Z axis (head tilt left/right)");
  client.println("      ");
  client.println("      cube.style.transform = ");
  client.println("        `rotateY(${smoothYaw}deg) rotateX(${smoothPitch}deg) rotateZ(${smoothRoll}deg)`;");
  client.println("    }");
  client.println("    ");
  client.println("    function smoothValue(axis, value) {");
  client.println("      // Add new value to history");
  client.println("      orientationHistory[axis].push(value);");
  client.println("      ");
  client.println("      // Keep only last 5 values");
  client.println("      if (orientationHistory[axis].length > 5) {");
  client.println("        orientationHistory[axis].shift();");
  client.println("      }");
  client.println("      ");
  client.println("      // Calculate average");
  client.println("      const sum = orientationHistory[axis].reduce((a, b) => a + b, 0);");
  client.println("      return sum / orientationHistory[axis].length;");
  client.println("    }");
  client.println("    ");
  client.println("    function updateSystemStatus(data) {");
  client.println("      // Update zero point status");
  client.println("      if (data.zeroSet) {");
  client.println("        document.getElementById('zeroStatus').textContent = 'Set';");
  client.println("        document.getElementById('zeroStatus').style.color = '#4CAF50';");
  client.println("      } else {");
  client.println("        document.getElementById('zeroStatus').textContent = 'Not Set';");
  client.println("        document.getElementById('zeroStatus').style.color = '#f44336';");
  client.println("      }");
  client.println("      ");
  client.println("      // Update WiFi signal if available");
  client.println("      if (data.signal) {");
  client.println("        document.getElementById('wifiSignal').textContent = data.signal + ' dBm';");
  client.println("        const signalColor = data.signal > -60 ? '#4CAF50' : data.signal > -70 ? '#ff9800' : '#f44336';");
  client.println("        document.getElementById('wifiSignal').style.color = signalColor;");
  client.println("      }");
  client.println("      ");
  client.println("      // Update device state based on movement");
  client.println("      const movement = Math.abs(data.relPitch) + Math.abs(data.relRoll) + Math.abs(data.relYaw);");
  client.println("      document.getElementById('deviceState').textContent = movement > 5 ? 'Active' : 'Idle';");
  client.println("      document.getElementById('deviceState').style.color = movement > 5 ? '#4CAF50' : '#ff9800';");
  client.println("    }");
  client.println("    ");
  client.println("    function sendCommand(command) {");
  client.println("      if (ws && ws.readyState === WebSocket.OPEN) {");
  client.println("        let message = '';");
  client.println("        switch(command) {");
  client.println("          case 'ledOn':");
  client.println("          case 'ledOff':");
  client.println("          case 'blink':");
  client.println("          case 'restart':");
  client.println("          case 'setZero':");
  client.println("          case 'resetZero':");
  client.println("          case 'resetYaw':");
  client.println("          case 'recalibrate':");
  client.println("          case 'resetAngles':");
  client.println("            message = JSON.stringify({ type: command });");
  client.println("            break;");
  client.println("        }");
  client.println("        if (message) {");
  client.println("          ws.send(message);");
  client.println("        }");
  client.println("      } else {");
  client.println("        showNotification('WebSocket not connected!', 'error');");
  client.println("      }");
  client.println("    }");
  client.println("    ");
  client.println("    function showNotification(message, type = 'info') {");
  client.println("      // Create notification element");
  client.println("      const notification = document.createElement('div');");
  client.println("      notification.textContent = message;");
  client.println("      notification.style.cssText = 'position: fixed;top: 20px;right: 20px; background: ' + (type === 'error' ? '#f44336' : type === 'warning' ? '#ff9800' : type === 'success' ? '#4CAF50' : '#2196F3') + ';color: white;padding: 15px 20px;border-radius: 5px;z-index: 1001;box-shadow: 0 4px 8px rgba(0,0,0,0.2);font-weight: bold;max-width: 300px;'");
  client.println("      ");
  client.println("      document.body.appendChild(notification);");
  client.println("      ");
  client.println("      // Remove after 3 seconds");
  client.println("      setTimeout(() => {");
  client.println("        if (notification.parentNode) {");
  client.println("          notification.parentNode.removeChild(notification);");
  client.println("        }");
  client.println("      }, 3000);");
  client.println("    }");
  client.println("    ");
  client.println("    // Initialize when page loads");
  client.println("    window.addEventListener('load', function() {");
  client.println("      connectWebSocket();");
  client.println("    });");
  client.println("  </script>");
  client.println("</body>");
  client.println("</html>");
  client.stop();
}

// Информация о системе
void handleInfo() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  String info = "Информация о системе:\n\n";
  info += "=== WiFi ===\n";
  info += "Статус: " + getWiFiStatus() + "\n";
  info += "SSID: " + WiFi.SSID() + "\n";
  info += "IP адрес: " + WiFi.localIP().toString() + "\n";
  info += "MAC адрес: " + WiFi.macAddress() + "\n";
  info += "Сила сигнала: " + getWiFiRSSI() + " dBm\n";
  info += "Шлюз: " + WiFi.gatewayIP().toString() + "\n";
  info += "DNS: " + WiFi.dnsIP().toString() + "\n\n";
  
  info += "=== Система ===\n";
  info += "Время работы: " + formatTime(millis() - startTime) + "\n";
  info += "Посетителей: " + String(visitorCount) + "\n";
  info += "Статус LED: " + String(ledState ? "ВКЛЮЧЕН" : "ВЫКЛЮЧЕН") + "\n";
  info += "ID чипа: " + String(ESP.getChipId()) + "\n";
  info += "Частота CPU: " + String(ESP.getCpuFreqMHz()) + " MHz\n";
  info += "Свободная память: " + String(ESP.getFreeHeap()) + " байт\n";
  info += "Размер Flash: " + String(ESP.getFlashChipSize()) + " байт\n\n";
  
  info += "=== MPU6050 ===\n";
  info += "Подключен: " + String(mpuConnected ? "Да" : "Нет") + "\n";
  info += "Калиброван: " + String(calibrated ? "Да" : "Нет") + "\n";
  info += "Авто-калибровка: " + String(autoCalibrationEnabled ? "Включена" : "Выключена") + "\n";
  info += "Нулевая точка: " + String(zeroSet ? "Установлена" : "Не установлена") + "\n";
  info += "Pitch: " + String(smoothedPitch, 2) + "°\n";
  info += "Roll: " + String(smoothedRoll, 2) + "°\n";
  info += "Yaw: " + String(smoothedYaw, 2) + "°\n";
  
  server.send(200, "text/plain", info);
}

// Сканирование WiFi сетей
void handleWiFiScan() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  String page = "<html><head><meta charset='UTF-8'><title>Сканирование WiFi</title></head><body>";
  page += "<h1>📡 Доступные WiFi сети</h1>";
  page += "<a href='/'>← Назад</a><br><br>";
  
  int n = WiFi.scanComplete();
  if (n == -2) {
    // Сканирование еще не запускалось
    WiFi.scanNetworks(true);
    page += "Сканирование запущено...<br>";
    page += "<script>setTimeout(function(){ location.reload(); }, 3000);</script>";
  } else if (n == -1) {
    // Сканирование выполняется
    page += "Сканирование выполняется...<br>";
    page += "<script>setTimeout(function(){ location.reload(); }, 3000);</script>";
  } else if (n == 0) {
    page += "Сети не найдены";
  } else {
    page += "Найдено сетей: " + String(n) + "<br><br>";
    page += "<table border='1' cellpadding='5'>";
    page += "<tr><th>SSID</th><th>Сигнал</th><th>Защита</th><th>Канал</th></tr>";
    
    for (int i = 0; i < n; ++i) {
      page += "<tr>";
      page += "<td>" + WiFi.SSID(i) + "</td>";
      page += "<td>" + String(WiFi.RSSI(i)) + " dBm</td>";
      page += "<td>" + String((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "Открытая" : "Защищенная") + "</td>";
      page += "<td>" + String(WiFi.channel(i)) + "</td>";
      page += "</tr>";
    }
    page += "</table>";
    
    // Сбрасываем сканирование для следующего раза
    WiFi.scanDelete();
  }
  
  page += "</body></html>";
  server.send(200, "text/html", page);
}

// Перезагрузка
void handleRestart() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", "<html><body><h1>Перезагрузка...</h1><p>ESP8266 перезагрузится через 3 секунды</p></body></html>");
  delay(3000);
  ESP.restart();
}

// Обработка несуществующих страниц
void handleNotFound() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  String message = "Страница не найдена\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nДоступные страницы:\n";
  message += "/ - Главная страница\n";
  message += "/info - Информация о системе\n";
  message += "/wifi-scan - Сканировать WiFi\n";
  message += "/restart - Перезагрузка\n";
  
  server.send(404, "text/plain", message);
}

void setup() {
  // Инициализация последовательного порта
  Serial.begin(115200);
  delay(1000);
  
  // Инициализация EEPROM и загрузка нулевой точки
  loadZeroPoint();
  
  // Настройка встроенного LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // LED включен (активный низкий уровень)
  ledState = LOW;
  
  // Подключение к WiFi
  Serial.println();
  Serial.println("Подключение к WiFi...");
  Serial.print("SSID: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  // Ожидание подключения
  Serial.print("Подключение");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // Мигаем LED во время подключения
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi подключен!");
    Serial.print("IP адрес: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_BUILTIN, LOW); // Выключаем LED после подключения
  } else {
    Serial.println("Не удалось подключиться к WiFi!");
    // Можно продолжить работу в режиме AP
    WiFi.softAP("ESP8266_Fallback", "12345678");
    Serial.print("Запущен резервный AP. IP: ");
    Serial.println(WiFi.softAPIP());
  }
  
  // Инициализация MPU6050
  mpuConnected = initializeMPU6050();
  if (mpuConnected) {
    calibrationStart = millis();
    Serial.println("🔧 Калибровка гироскопа... Держите сенсор неподвижно 3 секунды!");
  }
  
  // Настройка веб-сервера
  server.on("/", handleRoot);
  server.on("/info", handleInfo);
  server.on("/wifi-scan", handleWiFiScan);
  server.on("/restart", handleRestart);
  server.onNotFound(handleNotFound);
  
  // Запуск серверов
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  Serial.println("HTTP сервер запущен на порту 80");
  Serial.println("WebSocket сервер запущен на порту 81");
  
  // Запоминаем время старта
  startTime = millis();
  
  Serial.println("Готово! Откройте в браузере ваш IP адрес:");
  Serial.println(WiFi.localIP());
  Serial.println("WebSocket: ws://" + WiFi.localIP().toString() + ":81");
}

void loop() {
  // Обработка HTTP клиентов
  server.handleClient();
  
  // Обработка WebSocket клиентов
  webSocket.loop();
  
  // Обработка данных сенсора
  if (mpuConnected) {
    processSensorData();
  }
  
  // Отправка данных сенсора
  unsigned long currentTime = millis();
  if (currentTime - lastDataSend >= DATA_SEND_INTERVAL) {
    if (mpuConnected && calibrated) {
      sendSensorData();
    }
    lastDataSend = currentTime;
  }
  
  delay(10);
}
