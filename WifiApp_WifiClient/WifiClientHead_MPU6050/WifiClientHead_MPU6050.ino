#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <ArduinoJson.h>

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
  
  // Убрать комплементарный фильтр с акселерометром для pitch и roll
  // Акселерометр ограничивает углы ±180°, поэтому используем только гироскоп
  // float alpha = 0.96;
  // pitch = alpha * pitch + (1.0 - alpha) * accelPitch;
  // roll = alpha * roll + (1.0 - alpha) * accelRoll;
  
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
    
    // Уведомление клиентов
   // String statusMsg = "{\"type\":\"status\",\"message\":\"Калибровка завершена\"}";
   // webSocket.broadcastTXT(statusMsg);
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
  zeroPitch = smoothedPitch;
  zeroRoll = smoothedRoll;
  zeroYaw = smoothedYaw;
  zeroSet = true;
  
  Serial.printf("💾 Нулевая точка установлена - Pitch:%.1f° Roll:%.1f° Yaw:%.1f°\n", 
               zeroPitch, zeroRoll, zeroYaw);
}

// Сброс нулевой точки
void resetZeroPoint() {
  zeroPitch = 0;
  zeroRoll = 0;
  zeroYaw = 0;
  zeroSet = false;
  
  Serial.println("🔄 Нулевая точка сброшена");
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
        
        // Отправка приветственного сообщения
        //String welcome = "{\"type\":\"status\",\"message\":\"Подключен к MPU6050 трекеру\"}";
        //webSocket.sendTXT(num, welcome);
        
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
          //String response = "{\"type\":\"status\",\"message\":\"Нулевая точка установлена\"}";
          //webSocket.sendTXT(num, response);
          String zeroInfo = "{\"type\":\"zeroInfo\",\"zeroPitch\":" + String(zeroPitch, 2) + 
                           ",\"zeroRoll\":" + String(zeroRoll, 2) + 
                           ",\"zeroYaw\":" + String(zeroYaw, 2) + "}";
          webSocket.broadcastTXT(zeroInfo);
        }
        else if (command == "resetZero") {
          resetZeroPoint();
          //String response = "{\"type\":\"status\",\"message\":\"Нулевая точка сброшена\"}";
          //webSocket.sendTXT(num, response);
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
          //String response = "{\"type\":\"status\",\"message\":\"Перекалибровка запущена\"}";
          //webSocket.sendTXT(num, response);
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
        // В функции webSocketEvent в блоке WStype_TEXT добавить:
        } else if (command == "resetAngles") {
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
  client.println("<title>ESP8266 MPU6050 Demo</title>");
  client.println("<style>");
  client.println("body { ");
  client.println("  font-family: Arial, sans-serif; ");
  client.println("  margin: 0; ");
  client.println("  padding: 20px; ");
  client.println("  background-color: #f0f0f0;");
  client.println("  overflow-x: hidden;");
  client.println("}");
  client.println(".container { ");
  client.println("  max-width: 1200px; ");
  client.println("  margin: 0 auto; ");
  client.println("  background: white; ");
  client.println("  padding: 20px; ");
  client.println("  border-radius: 15px; ");
  client.println("  box-shadow: 0 0 10px rgba(0,0,0,0.1);");
  client.println("}");
  client.println("h1 { color: #333; text-align: center; }");
  client.println(".dashboard {");
  client.println("  display: grid;");
  client.println("  grid-template-columns: 1fr 1fr;");
  client.println("  gap: 20px;");
  client.println("  margin: 20px 0;");
  client.println("}");
  client.println(".info { ");
  client.println("  background: #e7f3ff; ");
  client.println("  padding: 15px; ");
  client.println("  border-radius: 8px;");
  client.println("  border-left: 4px solid #2196F3;");
  client.println("}");
  client.println(".sensor-data {");
  client.println("  background: #fff3cd;");
  client.println("  padding: 15px;");
  client.println("  border-radius: 8px;");
  client.println("  border-left: 4px solid #ffc107;");
  client.println("}");
  client.println(".button { ");
  client.println("  background: #4CAF50; ");
  client.println("  color: white; ");
  client.println("  padding: 10px 15px; ");
  client.println("  border: none; ");
  client.println("  border-radius: 5px; ");
  client.println("  cursor: pointer; ");
  client.println("  font-size: 14px;");
  client.println("  margin: 5px;");
  client.println("}");
  client.println(".button:hover { background: #45a049; }");
  client.println(".button-red { background: #f44336; }");
  client.println(".button-red:hover { background: #da190b; }");
  client.println(".button-blue { background: #2196F3; }");
  client.println(".button-blue:hover { background: #1976D2; }");
  client.println(".status { ");
  client.println("  padding: 10px; ");
  client.println("  margin: 10px 0; ");
  client.println("  border-radius: 5px;");
  client.println("  font-weight: bold;");
  client.println("}");
  client.println(".led-on { background: #4CAF50; color: white; }");
  client.println(".led-off { background: #666; color: white; }");
  client.println(".wifi-status { ");
  client.println("  padding: 8px; ");
  client.println("  margin: 5px 0; ");
  client.println("  border-radius: 5px;");
  client.println("  font-size: 14px;");
  client.println("}");
  client.println(".connected { background: #d4edda; color: #155724; }");
  client.println(".disconnected { background: #f8d7da; color: #721c24; }");
  client.println("#visualization {");
  client.println("  width: 100%;");
  client.println("  height: 400px;");
  client.println("  background: #2c3e50;");
  client.println("  border-radius: 8px;");
  client.println("  margin: 20px 0;");
  client.println("  position: relative;");
  client.println("  overflow: hidden;");
  client.println("}");
  client.println(".control-panel {");
  client.println("  display: grid;");
  client.println("  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));");
  client.println("  gap: 10px;");
  client.println("  margin: 20px 0;");
  client.println("}");
  client.println(".data-display {");
  client.println("  font-family: 'Courier New', monospace;");
  client.println("  background: #f8f9fa;");
  client.println("  padding: 10px;");
  client.println("  border-radius: 5px;");
  client.println("  margin: 5px 0;");
  client.println("}");
  client.println(".websocket-status {");
  client.println("  padding: 10px;");
  client.println("  border-radius: 5px;");
  client.println("  margin: 10px 0;");
  client.println("  text-align: center;");
  client.println("  font-weight: bold;");
  client.println("}");
  client.println(".ws-connected { background: #d4edda; color: #155724; }");
  client.println(".ws-disconnected { background: #f8d7da; color: #721c24; }");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  client.println("  <div class='container'>");
  client.println("    <h1>🚀 ESP8266 MPU6050 Sensor Demo</h1>");
  
  String wifiClass = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
  String wifiStatus = getWiFiStatus() + " | " + WiFi.SSID();
  client.println("    <div class='wifi-status " + wifiClass + "'>");
  client.println("      WiFi: " + wifiStatus);
  client.println("    </div>");

  client.println("    <div class='websocket-status' id='wsStatus'>");
  client.println("      WebSocket: Connecting...");
  client.println("    </div>");
  
  client.println("    <div class='dashboard'>");
  client.println("      <div class='info'>");
  client.println("        <h3>📊 Системная информация</h3>");
  client.println("        <p><strong>Время работы:</strong> " + formatTime(millis() - startTime) + "</p>");
  client.println("        <p><strong>Посетителей:</strong> " + String(visitorCount) + "</p>");
  client.println("        <p><strong>Статус LED:</strong> " + String(ledState ? "ВКЛЮЧЕН" : "ВЫКЛЮЧЕН") + "</p>");
  client.println("        <p><strong>IP адрес:</strong> " + WiFi.localIP().toString() + "</p>");
  client.println("        <p><strong>SSID сети:</strong> " + WiFi.SSID() + "</p>");
  client.println("        <p><strong>Сила сигнала:</strong> " + getWiFiRSSI() + " dBm</p>");
  client.println("        <p><strong>Чип ID:</strong> " + String(ESP.getChipId()) + "</p>");
  client.println("      </div>");

  client.println("      <div class='sensor-data'>");
  client.println("        <h3>🎯 Данные MPU6050</h3>");
  client.println("        <div class='data-display'>");
  client.println("          <div>Pitch: <span id='pitch'>0.00</span>°</div>");
  client.println("          <div>Roll: <span id='roll'>0.00</span>°</div>");
  client.println("          <div>Yaw: <span id='yaw'>0.00</span>°</div>");
  client.println("        </div>");
  client.println("        <div class='data-display'>");
  client.println("          <div>Относительный Pitch: <span id='relPitch'>0.00</span>°</div>");
  client.println("          <div>Относительный Roll: <span id='relRoll'>0.00</span>°</div>");
  client.println("          <div>Относительный Yaw: <span id='relYaw'>0.00</span>°</div>");
  client.println("        </div>");
  client.println("        <div class='data-display'>");
  client.println("          <div>Статус калибровки: <span id='calibrationStatus'>Калибруется...</span></div>");
  client.println("          <div>Авто-калибровка: <span id='autoCalStatus'>Включена</span></div>");
  client.println("          <div>Нулевая точка: <span id='zeroStatus'>Не установлена</span></div>");
  client.println("        </div>");
  client.println("      </div>");
  client.println("    </div>");

  client.println("    <div id='visualization'>");
  client.println("      <canvas id='cubeCanvas' width='800' height='400'></canvas>");
  client.println("    </div>");

  client.println("    <div class='control-panel'>");
  client.println("      <div>");
  client.println("        <h4>Управление LED</h4>");
  client.println("        <button class='button' onclick=\"sendCommand('ledOn')\">🟢 Включить LED</button>");
  client.println("        <button class='button button-red' onclick=\"sendCommand('ledOff')\">🔴 Выключить LED</button>");
  client.println("        <button class='button' onclick=\"sendCommand('blink')\">✨ Мигать LED</button>");
  client.println("      </div>");
  
  client.println("      <div>");
  client.println("        <h4>Управление сенсором</h4>");
  client.println("        <button class='button button-blue' onclick=\"sendCommand('setZero')\">🎯 Установить нулевую точку</button>");
  client.println("        <button class='button' onclick=\"sendCommand('resetZero')\">🔄 Сбросить нулевую точку</button>");
  client.println("        <button class='button' onclick=\"sendCommand('resetYaw')\">↩️ Сбросить Yaw</button>");
  client.println("        <button class='button' onclick=\"sendCommand('resetAngles')\">🔄 Сбросить все углы</button>");
  client.println("      </div>");
  
  client.println("      <div>");
  client.println("        <h4>Калибровка</h4>");
  client.println("        <button class='button' onclick=\"sendCommand('recalibrate')\">⚙️ Перекалибровать</button>");
  client.println("        <button class='button' id='autoCalBtn' onclick=\"toggleAutoCalibration()\">🔴 Выключить авто-калибровку</button>");
  client.println("      </div>");
  
  client.println("      <div>");
  client.println("        <h4>Система</h4>");
  client.println("        <button class='button' onclick=\"location.href='/info'\">ℹ️ Информация</button>");
  client.println("        <button class='button' onclick=\"sendCommand('restart')\">🔄 Перезагрузить</button>");
  client.println("      </div>");
  client.println("    </div>");

  String ledClass = ledState ? "led-on" : "led-off";
  String ledText = ledState ? "ВКЛЮЧЕН" : "ВЫКЛЮЧЕН";
  client.println("    <div class='status " + ledClass + "'>");
  client.println("      LED: " + ledText);
  client.println("    </div>");

  client.println("    <div style='margin-top: 30px; font-size: 14px; color: #666; text-align: center;'>");
  client.println("      <p>ESP8266 MPU6050 Sensor | Версия 3.0</p>");
  client.println("    </div>");
  client.println("  </div>");

  client.println("  <script>");
  client.println("    let ws = null;");
  client.println("    let cubeCanvas, ctx;");
  client.println("    let sensorData = { pitch: 0, roll: 0, yaw: 0, relPitch: 0, relRoll: 0, relYaw: 0 };");
  client.println("");
  client.println("    function connectWebSocket() {");
  client.println("      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';");
  client.println("      const wsUrl = `${protocol}//${window.location.hostname}:81`;");
  client.println("      ");
  client.println("      ws = new WebSocket(wsUrl);");
  client.println("      ");
  client.println("      ws.onopen = function() {");
  client.println("        document.getElementById('wsStatus').className = 'websocket-status ws-connected';");
  client.println("        document.getElementById('wsStatus').textContent = 'WebSocket: Connected';");
  client.println("        console.log('WebSocket connected');");
  client.println("      };");
  client.println("      ");
  client.println("      ws.onclose = function() {");
  client.println("        document.getElementById('wsStatus').className = 'websocket-status ws-disconnected';");
  client.println("        document.getElementById('wsStatus').textContent = 'WebSocket: Disconnected';");
  client.println("        console.log('WebSocket disconnected');");
  client.println("        // Попытка переподключения через 3 секунды");
  client.println("        setTimeout(connectWebSocket, 3000);");
  client.println("      };");
  client.println("      ");
  client.println("      ws.onmessage = function(event) {");
  client.println("        try {");
  client.println("          const data = JSON.parse(event.data);");
  client.println("          handleWebSocketMessage(data);");
  client.println("        } catch (e) {");
  client.println("          console.error('Error parsing WebSocket message:', e);");
  client.println("        }");
  client.println("      };");
  client.println("      ");
  client.println("      ws.onerror = function(error) {");
  client.println("        console.error('WebSocket error:', error);");
  client.println("      };");
  client.println("    }");
  client.println("");
  client.println("    function handleWebSocketMessage(data) {");
  client.println("      if (data.type === 'sensorData') {");
  client.println("        // Обновление данных сенсора");
  client.println("        sensorData = data;");
  client.println("        updateSensorDisplay();");
  client.println("        drawCube();");
  client.println("      } else if (data.type === 'status') {");
  client.println("        console.log('Status:', data.message);");
  client.println("        alert(data.message);");
  client.println("      } else if (data.type === 'calibrationStatus') {");
  client.println("        document.getElementById('calibrationStatus').textContent = data.calibrated ? 'Готов' : 'Калибруется...';");
  client.println("      } else if (data.type === 'autoCalibrationStatus') {");
  client.println("        const btn = document.getElementById('autoCalBtn');");
  client.println("        btn.textContent = data.enabled ? '🔴 Выключить авто-калибровку' : '🟢 Включить авто-калибровку';");
  client.println("        document.getElementById('autoCalStatus').textContent = data.enabled ? 'Включена' : 'Выключена';");
  client.println("      } else if (data.type === 'zeroInfo') {");
  client.println("        document.getElementById('zeroStatus').textContent = 'Установлена';");
  client.println("      } else if (data.type === 'zeroReset') {");
  client.println("        document.getElementById('zeroStatus').textContent = 'Не установлена';");
  client.println("      }");
  client.println("    }");
  client.println("");
  client.println("    function updateSensorDisplay() {");
  client.println("      document.getElementById('pitch').textContent = sensorData.pitch.toFixed(2);");
  client.println("      document.getElementById('roll').textContent = sensorData.roll.toFixed(2);");
  client.println("      document.getElementById('yaw').textContent = sensorData.yaw.toFixed(2);");
  client.println("      document.getElementById('relPitch').textContent = sensorData.relPitch.toFixed(2);");
  client.println("      document.getElementById('relRoll').textContent = sensorData.relRoll.toFixed(2);");
  client.println("      document.getElementById('relYaw').textContent = sensorData.relYaw.toFixed(2);");
  client.println("    }");
  client.println("");
  client.println("    function sendCommand(command) {");
  client.println("      if (ws && ws.readyState === WebSocket.OPEN) {");
  client.println("        let message = '';");
  client.println("        switch(command) {");
  client.println("          case 'ledOn':");
  client.println("          case 'ledOff':");
  client.println("          case 'blink':");
  client.println("          case 'restart':");
  client.println("            message = JSON.stringify({ type: command });");
  client.println("            break;");
  client.println("          case 'setZero':");
  client.println("          case 'resetZero':");
  client.println("          case 'resetYaw':");
  client.println("          case 'recalibrate':");
  client.println("            message = JSON.stringify({ type: command });");
  client.println("            break;");
  client.println("        }");
  client.println("        if (message) {");
  client.println("          ws.send(message);");
  client.println("        }");
  client.println("      } else {");
  client.println("        alert('WebSocket не подключен!');");
  client.println("      }");
  client.println("    }");
  client.println("");
  client.println("    function toggleAutoCalibration() {");
  client.println("      if (ws && ws.readyState === WebSocket.OPEN) {");
  client.println("        const btn = document.getElementById('autoCalBtn');");
  client.println("        const currentlyEnabled = btn.textContent.includes('Выключить');");
  client.println("        ws.send(JSON.stringify({ ");
  client.println("          type: 'setAutoCalibration', ");
  client.println("          enable: !currentlyEnabled ");
  client.println("        }));");
  client.println("      }");
  client.println("    }");
  client.println("");
  client.println("    // 3D визуализация куба");
  client.println("    function initCube() {");
  client.println("      cubeCanvas = document.getElementById('cubeCanvas');");
  client.println("      ctx = cubeCanvas.getContext('2d');");
  client.println("      ");
  client.println("      // Адаптивный размер canvas");
  client.println("      function resizeCanvas() {");
  client.println("        const container = document.getElementById('visualization');");
  client.println("        cubeCanvas.width = container.clientWidth;");
  client.println("        cubeCanvas.height = container.clientHeight;");
  client.println("      }");
  client.println("      ");
  client.println("      window.addEventListener('resize', resizeCanvas);");
  client.println("      resizeCanvas();");
  client.println("    }");
  client.println("");
  client.println("    function drawCube() {");
  client.println("      if (!ctx) return;");
  client.println("      ");
  client.println("      const width = cubeCanvas.width;");
  client.println("      const height = cubeCanvas.height;");
  client.println("      const centerX = width / 2;");
  client.println("      const centerY = height / 2;");
  client.println("      const size = Math.min(width, height) * 0.2;");
  client.println("      ");
  client.println("      // Очистка canvas");
  client.println("      ctx.fillStyle = '#2c3e50';");
  client.println("      ctx.fillRect(0, 0, width, height);");
  client.println("      ");
  client.println("      // Используем абсолютные углы вместо относительных");
  client.println("      // Преобразование углов в радианы (убираем нормализацию)");
  client.println("      const pitchRad = (sensorData.pitch % 360) * Math.PI / 180;");
  client.println("      const rollRad = (sensorData.roll % 360) * Math.PI / 180;");
  client.println("      const yawRad = (sensorData.yaw % 360) * Math.PI / 180;");
  client.println("      ");
  client.println("      // Вершины куба");
  client.println("      const vertices = [");
  client.println("        { x: -size, y: -size, z: -size },");
  client.println("        { x: size, y: -size, z: -size },");
  client.println("        { x: size, y: size, z: -size },");
  client.println("        { x: -size, y: size, z: -size },");
  client.println("        { x: -size, y: -size, z: size },");
  client.println("        { x: size, y: -size, z: size },");
  client.println("        { x: size, y: size, z: size },");
  client.println("        { x: -size, y: size, z: size }");
  client.println("      ];");
  client.println("      ");
  client.println("      // Проекция 3D в 2D");
  client.println("      function project(point) {");
  client.println("        // Поворот по осям");
  client.println("        let x = point.x;");
  client.println("        let y = point.y;");
  client.println("        let z = point.z;");
  client.println("        ");
  client.println("        // Поворот вокруг X (pitch)");
  client.println("        const cosPitch = Math.cos(pitchRad);");
  client.println("        const sinPitch = Math.sin(pitchRad);");
  client.println("        let y1 = y * cosPitch - z * sinPitch;");
  client.println("        let z1 = y * sinPitch + z * cosPitch;");
  client.println("        ");
  client.println("        // Поворот вокруг Y (roll)");
  client.println("        const cosRoll = Math.cos(rollRad);");
  client.println("        const sinRoll = Math.sin(rollRad);");
  client.println("        let x1 = x * cosRoll + z1 * sinRoll;");
  client.println("        let z2 = -x * sinRoll + z1 * cosRoll;");
  client.println("        ");
  client.println("        // Поворот вокруг Z (yaw)");
  client.println("        const cosYaw = Math.cos(yawRad);");
  client.println("        const sinYaw = Math.sin(yawRad);");
  client.println("        let x2 = x1 * cosYaw - y1 * sinYaw;");
  client.println("        let y2 = x1 * sinYaw + y1 * cosYaw;");
  client.println("        ");
  client.println("        // Перспективная проекция");
  client.println("        const perspective = 500;");
  client.println("        const scale = perspective / (perspective + z2);");
  client.println("        ");
  client.println("        return {");
  client.println("          x: centerX + x2 * scale,");
  client.println("          y: centerY + y2 * scale");
  client.println("        };");
  client.println("      }");
  client.println("      ");
  client.println("      // Проецируем все вершины");
  client.println("      const projected = vertices.map(project);");
  client.println("      ");
  client.println("      // Рисуем грани");
  client.println("      const faces = [");
  client.println("        [0, 1, 2, 3], // задняя");
  client.println("        [4, 5, 6, 7], // передняя");
  client.println("        [0, 4, 7, 3], // левая");
  client.println("        [1, 5, 6, 2], // правая");
  client.println("        [0, 1, 5, 4], // нижняя");
  client.println("        [3, 2, 6, 7]  // верхняя");
  client.println("      ];");
  client.println("      ");
  client.println("      const colors = ['#e74c3c', '#3498db', '#2ecc71', '#f39c12', '#9b59b6', '#1abc9c'];");
  client.println("      ");
  client.println("      faces.forEach((face, index) => {");
  client.println("        ctx.fillStyle = colors[index];");
  client.println("        ctx.strokeStyle = '#34495e';");
  client.println("        ctx.lineWidth = 2;");
  client.println("        ");
  client.println("        ctx.beginPath();");
  client.println("        ctx.moveTo(projected[face[0]].x, projected[face[0]].y);");
  client.println("        for (let i = 1; i < face.length; i++) {");
  client.println("          ctx.lineTo(projected[face[i]].x, projected[face[i]].y);");
  client.println("        }");
  client.println("        ctx.closePath();");
  client.println("        ctx.fill();");
  client.println("        ctx.stroke();");
  client.println("      });");
  client.println("      ");
  client.println("      // Рисуем оси");
  client.println("      drawAxes();");
  client.println("    }");
  client.println("");
  client.println("    function drawAxes() {");
  client.println("      const length = 100;");
  client.println("      const origin = { x: 0, y: 0, z: 0 };");
  client.println("      const xAxis = { x: length, y: 0, z: 0 };");
  client.println("      const yAxis = { x: 0, y: length, z: 0 };");
  client.println("      const zAxis = { x: 0, y: 0, z: length };");
  client.println("      ");
  client.println("      const projOrigin = project(origin);");
  client.println("      const projX = project(xAxis);");
  client.println("      const projY = project(yAxis);");
  client.println("      const projZ = project(zAxis);");
  client.println("      ");
  client.println("      // Ось X (красная)");
  client.println("      ctx.strokeStyle = '#e74c3c';");
  client.println("      ctx.lineWidth = 3;");
  client.println("      ctx.beginPath();");
  client.println("      ctx.moveTo(projOrigin.x, projOrigin.y);");
  client.println("      ctx.lineTo(projX.x, projX.y);");
  client.println("      ctx.stroke();");
  client.println("      ");
  client.println("      // Ось Y (зеленая)");
  client.println("      ctx.strokeStyle = '#2ecc71';");
  client.println("      ctx.beginPath();");
  client.println("      ctx.moveTo(projOrigin.x, projOrigin.y);");
  client.println("      ctx.lineTo(projY.x, projY.y);");
  client.println("      ctx.stroke();");
  client.println("      ");
  client.println("      // Ось Z (синяя)");
  client.println("      ctx.strokeStyle = '#3498db';");
  client.println("      ctx.beginPath();");
  client.println("      ctx.moveTo(projOrigin.x, projOrigin.y);");
  client.println("      ctx.lineTo(projZ.x, projZ.y);");
  client.println("      ctx.stroke();");
  client.println("    }");
  client.println("");
  client.println("    // Функция проекции для осей (дублирует основную функцию проекции)");
  client.println("    function project(point) {");
  client.println("      const pitchRad = sensorData.relPitch * Math.PI / 180;");
  client.println("      const rollRad = sensorData.relRoll * Math.PI / 180;");
  client.println("      const yawRad = sensorData.relYaw * Math.PI / 180;");
  client.println("      ");
  client.println("      const width = cubeCanvas.width;");
  client.println("      const height = cubeCanvas.height;");
  client.println("      const centerX = width / 2;");
  client.println("      const centerY = height / 2;");
  client.println("      ");
  client.println("      let x = point.x;");
  client.println("      let y = point.y;");
  client.println("      let z = point.z;");
  client.println("      ");
  client.println("      const cosPitch = Math.cos(pitchRad);");
  client.println("      const sinPitch = Math.sin(pitchRad);");
  client.println("      let y1 = y * cosPitch - z * sinPitch;");
  client.println("      let z1 = y * sinPitch + z * cosPitch;");
  client.println("      ");
  client.println("      const cosRoll = Math.cos(rollRad);");
  client.println("      const sinRoll = Math.sin(rollRad);");
  client.println("      let x1 = x * cosRoll + z1 * sinRoll;");
  client.println("      let z2 = -x * sinRoll + z1 * cosRoll;");
  client.println("      ");
  client.println("      const cosYaw = Math.cos(yawRad);");
  client.println("      const sinYaw = Math.sin(yawRad);");
  client.println("      let x2 = x1 * cosYaw - y1 * sinYaw;");
  client.println("      let y2 = x1 * sinYaw + y1 * cosYaw;");
  client.println("      ");
  client.println("      const perspective = 500;");
  client.println("      const scale = perspective / (perspective + z2);");
  client.println("      ");
  client.println("      return {");
  client.println("        x: centerX + x2 * scale,");
  client.println("        y: centerY + y2 * scale");
  client.println("      };");
  client.println("    }");
  client.println("");
  client.println("    // Инициализация при загрузке страницы");
  client.println("    document.addEventListener('DOMContentLoaded', function() {");
  client.println("      initCube();");
  client.println("      connectWebSocket();");
  client.println("      // Запуск анимации");
  client.println("      setInterval(drawCube, 50);");
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
