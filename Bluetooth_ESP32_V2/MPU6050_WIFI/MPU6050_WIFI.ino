#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <SPIFFS.h>

Adafruit_MPU6050 mpu;

// UUID для службы и характеристики
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define DEVICE_NAME         "ESP32_MPU6050_BLE"

void sendSensorData();
void calibrateSensor();
void resetZeroPoint();
void setZeroPoint();
void scanI2C();


BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Sensor data
float pitch = 0, roll = 0, yaw = 0;
float lastSentPitch = 0, lastSentRoll = 0, lastSentYaw = 0;
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
bool calibrated = false;
unsigned long lastTime = 0;

// Относительный ноль
float zeroPitch = 0, zeroRoll = 0, zeroYaw = 0;
bool zeroSet = false;

// Накопленные углы (без ограничений)
double accumulatedPitch = 0, accumulatedRoll = 0, accumulatedYaw = 0;
float prevPitch = 0, prevRoll = 0, prevYaw = 0;
bool firstMeasurement = true;

// I2C адрес MPU6050
uint8_t mpuAddress = 0x68; // Адрес по умолчанию
bool mpuFound = false;

// WebSocket connection management
bool clientConnected = false;
unsigned long lastDataSend = 0;
const unsigned long SEND_INTERVAL = 50;
const float CHANGE_THRESHOLD = 1.0;

// Флаг для отправки данных
bool shouldSendData = false;
unsigned long lastWebSocketData = 0;

// Буфер для данных
String sensorDataString = "";

// HTML страница
String htmlPage = "";

// Класс коллбэков для сервера
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Device connected via BLE");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Device disconnected");
    }
};

// Класс коллбэков для характеристики
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      // Получаем значение как массив байт
      uint8_t* data = pCharacteristic->getData();
      size_t length = pCharacteristic->getLength();
      
      if (length > 0) {
        // Конвертируем байты в String
        String value = "";
        for (size_t i = 0; i < length; i++) {
          value += (char)data[i];
        }
        
        Serial.print("Received via BLE: ");
        Serial.println(value);
        
        // Обработка команд
        if (value == "GET_DATA") {
          shouldSendData = true;
          sendSensorData();
        }
        else if (value == "RECALIBRATE") {
          calibrated = false;
          calibrateSensor();
          String calMessage = "RECALIBRATION_COMPLETE";
          pCharacteristic->setValue(calMessage.c_str());
          pCharacteristic->notify();
        }
        else if (value == "RESET_ANGLES") {
          pitch = 0; roll = 0; yaw = 0;
          lastSentPitch = 0; lastSentRoll = 0; lastSentYaw = 0;
          resetZeroPoint();
          String resetMessage = "ANGLES_RESET";
          pCharacteristic->setValue(resetMessage.c_str());
          pCharacteristic->notify();
          sendSensorData();
        }
        else if (value == "SET_ZERO") {
          setZeroPoint();
          pCharacteristic->setValue("ZERO_POINT_SET");
          pCharacteristic->notify();
        }
        else if (value == "RESET_ZERO") {
          resetZeroPoint();
          pCharacteristic->setValue("ZERO_POINT_RESET");
          pCharacteristic->notify();
        }
        else if (value == "LED ON") {
          digitalWrite(2, HIGH); // Включаем встроенный LED
          pCharacteristic->setValue("LED_ON");
          pCharacteristic->notify();
        }
        else if (value == "LED OFF") {
          digitalWrite(2, LOW); // Выключаем встроенный LED
          pCharacteristic->setValue("LED_OFF");
          pCharacteristic->notify();
        }
        else if (value == "START_STREAM") {
          shouldSendData = true;
          pCharacteristic->setValue("STREAM_STARTED");
          pCharacteristic->notify();
        }
        else if (value == "STOP_STREAM") {
          shouldSendData = false;
          pCharacteristic->setValue("STREAM_STOPPED");
          pCharacteristic->notify();
        }
        else if (value == "SCAN_I2C") {
          scanI2C();
          String scanMessage = "I2C_SCAN_COMPLETE:ADDR:0x";
          if (mpuAddress < 16) scanMessage += "0";
          scanMessage += String(mpuAddress, HEX);
          scanMessage += ",FOUND:" + String(mpuFound ? "true" : "false");
          pCharacteristic->setValue(scanMessage.c_str());
          pCharacteristic->notify();
        }
        else if (value == "STATUS") {
          String status = "STATUS:MPU6050:" + String(mpuFound ? "FOUND" : "NOT_FOUND") + 
                         ",CALIBRATED:" + String(calibrated ? "YES" : "NO") + 
                         ",UPTIME:" + String(millis() / 1000) + "s";
          pCharacteristic->setValue(status.c_str());
          pCharacteristic->notify();
        }
        else {
          // Эхо-ответ для неизвестных команд
          String echo = "ECHO:" + value;
          pCharacteristic->setValue(echo.c_str());
          pCharacteristic->notify();
        }
      }
    }
};

// Функция сканирования I2C
void scanI2C() {
  Serial.println("Scanning I2C bus...");
  byte error, address;
  int foundDevices = 0;
  
  for(address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.print(" (");
      
      // Определяем тип устройства
      if (address == 0x68 || address == 0x69) {
        Serial.print("MPU6050/MPU9250");
        mpuAddress = address;
        mpuFound = true;
      } else if (address == 0x1E) {
        Serial.print("HMC5883L");
      } else if (address == 0x76 || address == 0x77) {
        Serial.print("BMP180/BMP280");
      } else if (address == 0x27 || address == 0x3F) {
        Serial.print("LCD Display");
      } else {
        Serial.print("Unknown device");
      }
      
      Serial.println(")");
      foundDevices++;
    }
    else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  
  if (foundDevices == 0) {
    Serial.println("No I2C devices found!");
  } else {
    Serial.println("Scan completed.");
    if (mpuFound) {
      Serial.print("MPU6050 found at address: 0x");
      if (mpuAddress < 16) Serial.print("0");
      Serial.println(mpuAddress, HEX);
    } else {
      Serial.println("MPU6050 not found!");
    }
  }
}

// Установка относительного нуля
void setZeroPoint() {
  zeroPitch = pitch;
  zeroRoll = roll;
  zeroYaw = yaw;
  zeroSet = true;
  
  // Сбрасываем накопленные углы при установке нуля
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  prevPitch = pitch;
  prevRoll = roll;
  prevYaw = yaw;
  
  Serial.println("Zero point set");
  Serial.print("Zero Pitch: "); Serial.print(zeroPitch);
  Serial.print(" Roll: "); Serial.print(zeroRoll);
  Serial.print(" Yaw: "); Serial.println(zeroYaw);
  
  sensorDataString = "ZERO_SET:PITCH:" + String(zeroPitch, 2) + 
                   ",ROLL:" + String(zeroRoll, 2) + 
                   ",YAW:" + String(zeroYaw, 2);
  
  // Отправляем через BLE
  if (deviceConnected && pCharacteristic) {
    pCharacteristic->setValue(sensorDataString.c_str());
    pCharacteristic->notify();
  }
}

// Сброс относительного нуля
void resetZeroPoint() {
  zeroPitch = 0;
  zeroRoll = 0;
  zeroYaw = 0;
  zeroSet = false;
  
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  prevPitch = pitch;
  prevRoll = roll;
  prevYaw = yaw;
  
  Serial.println("Zero point reset");
  
  if (deviceConnected && pCharacteristic) {
    pCharacteristic->setValue("ZERO_RESET");
    pCharacteristic->notify();
  }
}

// Расчет накопленных углов (без ограничений)
void updateAccumulatedAngles() {
  if (firstMeasurement) {
    prevPitch = pitch;
    prevRoll = roll;
    prevYaw = yaw;
    firstMeasurement = false;
    return;
  }
  
  // Вычисляем разницу углов с учетом переходов через 180/-180
  float deltaPitch = pitch - prevPitch;
  float deltaRoll = roll - prevRoll;
  float deltaYaw = yaw - prevYaw;
  
  // Корректируем разницу для переходов через границу ±180
  if (deltaPitch > 180) deltaPitch -= 360;
  else if (deltaPitch < -180) deltaPitch += 360;
  
  if (deltaRoll > 180) deltaRoll -= 360;
  else if (deltaRoll < -180) deltaRoll += 360;
  
  if (deltaYaw > 180) deltaYaw -= 360;
  else if (deltaYaw < -180) deltaYaw += 360;
  
  // Накопление углов
  accumulatedPitch += deltaPitch;
  accumulatedRoll += deltaRoll;
  accumulatedYaw += deltaYaw;
  
  prevPitch = pitch;
  prevRoll = roll;
  prevYaw = yaw;
}

// Получение относительных углов (без ограничений)
double getRelativePitch() {
  if (!zeroSet) return accumulatedPitch;
  return accumulatedPitch - zeroPitch;
}

double getRelativeRoll() {
  if (!zeroSet) return accumulatedRoll;
  return accumulatedRoll - zeroRoll;
}

double getRelativeYaw() {
  if (!zeroSet) return accumulatedYaw;
  return accumulatedYaw - zeroYaw;
}

void calibrateSensor() {
  Serial.println("Calibrating...");
  float sumX = 0, sumY = 0, sumZ = 0;
  
  for (int i = 0; i < 500; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sumX += g.gyro.x;
    sumY += g.gyro.y;
    sumZ += g.gyro.z;
    delay(2);
  }
  
  gyroOffsetX = sumX / 500;
  gyroOffsetY = sumY / 500;
  gyroOffsetZ = sumZ / 500;
  calibrated = true;
  
  Serial.println("Calibration complete");
}

void sendSensorData() {
  // Обновляем накопленные углы
  updateAccumulatedAngles();
  
  // Получаем относительные углы
  double relPitch = getRelativePitch();
  double relRoll = getRelativeRoll();
  double relYaw = getRelativeYaw();
  
  // Формируем строку с данными
  sensorDataString = "PITCH:" + String(pitch, 1) + 
                ",ROLL:" + String(roll, 1) + 
                ",YAW:" + String(yaw, 1) +
                ",REL_PITCH:" + String(relPitch, 2) +
                ",REL_ROLL:" + String(relRoll, 2) +
                ",REL_YAW:" + String(relYaw, 2) +
                ",ACC_PITCH:" + String(accumulatedPitch, 2) +
                ",ACC_ROLL:" + String(accumulatedRoll, 2) +
                ",ACC_YAW:" + String(accumulatedYaw, 2) +
                ",ZERO_SET:" + String(zeroSet ? "true" : "false") +
                ",MPU_FOUND:" + String(mpuFound ? "true" : "false") +
                ",TIME:" + String(millis());
  
  // Отправляем через BLE
  if (deviceConnected && pCharacteristic) {
    pCharacteristic->setValue(sensorDataString.c_str());
    pCharacteristic->notify();
  }
  
  lastSentPitch = pitch;
  lastSentRoll = roll;
  lastSentYaw = yaw;
  lastDataSend = millis();
}

bool dataChanged() {
  return (abs(pitch - lastSentPitch) >= CHANGE_THRESHOLD ||
          abs(roll - lastSentRoll) >= CHANGE_THRESHOLD ||
          abs(yaw - lastSentYaw) >= CHANGE_THRESHOLD);
}

// Загрузка HTML страницы из SPIFFS
String loadHTMLFromSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return "";
  }
  
  File file = SPIFFS.open("/index_2.html", "r");
  if (!file) {
    Serial.println("Failed to open HTML file");
    return "";
  }
  
  String html = "";
  while (file.available()) {
    html += char(file.read());
  }
  file.close();
  
  Serial.println("HTML file loaded successfully");
  return html;
}

// Создание HTML страницы (резервный вариант если нет SPIFFS)
String createHTMLPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 MPU6050 BLE Control</title>
    <style>
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            line-height: 1.6;
            color: #333;
            background-color: #f5f5f5;
            padding: 20px;
            max-width: 1200px;
            margin: 0 auto;
        }
        
        .container {
            background: white;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            padding: 30px;
        }
        
        h1 {
            color: #2c3e50;
            text-align: center;
            margin-bottom: 30px;
            padding-bottom: 15px;
            border-bottom: 2px solid #3498db;
        }
        
        .status {
            padding: 15px;
            border-radius: 5px;
            margin: 15px 0;
            text-align: center;
            font-weight: bold;
            font-size: 1.1em;
        }
        
        .status.connected {
            background-color: #d4edda;
            color: #155724;
            border: 1px solid #c3e6cb;
        }
        
        .status.disconnected {
            background-color: #f8d7da;
            color: #721c24;
            border: 1px solid #f5c6cb;
        }
        
        .status.scanning {
            background-color: #d1ecf1;
            color: #0c5460;
            border: 1px solid #bee5eb;
        }
        
        .btn-group {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            margin: 20px 0;
            justify-content: center;
        }
        
        .btn {
            background-color: #3498db;
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 5px;
            cursor: pointer;
            font-size: 16px;
            transition: all 0.3s;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
        }
        
        .btn:hover:not(:disabled) {
            background-color: #2980b9;
            transform: translateY(-2px);
        }
        
        .btn:disabled {
            background-color: #95a5a6;
            cursor: not-allowed;
            opacity: 0.6;
        }
        
        .btn-success {
            background-color: #2ecc71;
        }
        
        .btn-success:hover:not(:disabled) {
            background-color: #27ae60;
        }
        
        .btn-danger {
            background-color: #e74c3c;
        }
        
        .btn-danger:hover:not(:disabled) {
            background-color: #c0392b;
        }
        
        .control-panel {
            margin-top: 30px;
            padding: 20px;
            background-color: #f8f9fa;
            border-radius: 5px;
            border: 1px solid #e9ecef;
        }
        
        .input-group {
            margin-bottom: 20px;
        }
        
        label {
            display: block;
            margin-bottom: 8px;
            font-weight: bold;
            color: #495057;
        }
        
        input[type="text"] {
            width: 100%;
            padding: 12px;
            border: 2px solid #ced4da;
            border-radius: 5px;
            font-size: 16px;
            transition: border-color 0.3s;
        }
        
        input[type="text"]:focus {
            outline: none;
            border-color: #3498db;
        }
        
        .quick-commands {
            margin: 25px 0;
        }
        
        .quick-commands h3 {
            margin-bottom: 15px;
            color: #34495e;
        }
        
        .quick-btn {
            flex: 1;
            min-width: 150px;
            margin: 5px;
        }
        
        .log-container {
            margin-top: 30px;
        }
        
        .log-title {
            font-weight: bold;
            margin-bottom: 10px;
            color: #495057;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .log {
            border: 1px solid #ddd;
            border-radius: 5px;
            padding: 15px;
            background-color: #f8f9fa;
            height: 300px;
            overflow-y: auto;
        }
        
        .log-entry {
            padding: 8px;
            border-bottom: 1px solid #eee;
            font-family: 'Consolas', 'Monaco', monospace;
            font-size: 14px;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        
        .log-entry:last-child {
            border-bottom: none;
        }
        
        .log-time {
            color: #7f8c8d;
            font-size: 12px;
            min-width: 70px;
        }
        
        .log-message {
            flex: 1;
        }
        
        .log-sent {
            color: #3498db;
        }
        
        .log-received {
            color: #2ecc71;
        }
        
        .log-error {
            color: #e74c3c;
        }
        
        .log-info {
            color: #f39c12;
        }
        
        .device-info {
            background-color: #e8f4f8;
            padding: 15px;
            border-radius: 5px;
            margin: 15px 0;
            border-left: 4px solid #3498db;
        }
        
        .device-info h3 {
            margin-bottom: 10px;
            color: #2c3e50;
        }
        
        .device-details {
            font-family: monospace;
            font-size: 14px;
        }
        
        .data-display {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 15px;
            margin: 20px 0;
        }
        
        .data-card {
            background: white;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            border-left: 4px solid #3498db;
        }
        
        .data-card h3 {
            color: #2c3e50;
            margin-bottom: 15px;
            font-size: 16px;
        }
        
        .data-value {
            font-size: 24px;
            font-weight: bold;
            color: #2c3e50;
            margin: 5px 0;
        }
        
        .data-label {
            color: #7f8c8d;
            font-size: 12px;
            margin-top: 5px;
        }
        
        .sensor-container {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 20px;
            border-radius: 10px;
            margin: 20px 0;
        }
        
        .sensor-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-top: 20px;
        }
        
        .sensor-item {
            background: rgba(255, 255, 255, 0.1);
            padding: 15px;
            border-radius: 8px;
            backdrop-filter: blur(10px);
        }
        
        .sensor-value {
            font-size: 24px;
            font-weight: bold;
            color: #4cd964;
        }
        
        .visualization {
            margin: 20px 0;
            padding: 20px;
            background: #f8f9fa;
            border-radius: 10px;
        }
        
        .cube-container {
            width: 200px;
            height: 200px;
            margin: 20px auto;
            perspective: 1000px;
        }
        
        .cube {
            width: 100%;
            height: 100%;
            position: relative;
            transform-style: preserve-3d;
            transition: transform 0.1s ease-out;
        }
        
        .face {
            position: absolute;
            width: 200px;
            height: 200px;
            border: 2px solid #34495e;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 16px;
            font-weight: bold;
            color: white;
            background: rgba(52, 152, 219, 0.8);
        }
        
        .front { transform: rotateY(0deg) translateZ(100px); background: rgba(231, 76, 60, 0.8); }
        .back { transform: rotateY(180deg) translateZ(100px); background: rgba(52, 152, 219, 0.8); }
        .right { transform: rotateY(90deg) translateZ(100px); background: rgba(46, 204, 113, 0.8); }
        .left { transform: rotateY(-90deg) translateZ(100px); background: rgba(155, 89, 182, 0.8); }
        .top { transform: rotateX(90deg) translateZ(100px); background: rgba(241, 196, 15, 0.8); }
        .bottom { transform: rotateX(-90deg) translateZ(100px); background: rgba(230, 126, 34, 0.8); }
        
        .zero-controls {
            background: #e8f5e8;
            padding: 20px;
            margin: 20px 0;
            border-radius: 10px;
            border-left: 5px solid #28a745;
        }
        
        @media (max-width: 768px) {
            .btn-group {
                flex-direction: column;
            }
            
            .btn {
                width: 100%;
            }
            
            .container {
                padding: 15px;
            }
            
            .data-display {
                grid-template-columns: 1fr;
            }
            
            .sensor-grid {
                grid-template-columns: 1fr;
            }
            
            .cube-container {
                width: 150px;
                height: 150px;
            }
            
            .face {
                width: 150px;
                height: 150px;
                font-size: 12px;
            }
            
            .front, .back, .right, .left, .top, .bottom {
                transform: translateZ(75px);
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>📱 ESP32 MPU6050 BLE Control</h1>
        
        <div id="status" class="status disconnected">
            <span class="indicator offline"></span>🔌 Отключено от устройства
        </div>
        
        <div class="device-info" id="deviceInfo" style="display: none;">
            <h3>📱 Информация об устройстве:</h3>
            <div class="device-details" id="deviceDetails"></div>
        </div>
        
        <div class="btn-group">
            <button id="scanBtn" class="btn">
                <span>🔍</span> Сканировать BLE устройства
            </button>
            <button id="connectBtn" class="btn btn-success" disabled>
                <span>📱</span> Подключиться
            </button>
            <button id="disconnectBtn" class="btn btn-danger" disabled>
                <span>❌</span> Отключиться
            </button>
        </div>
        
        <div class="control-panel">
            <h2>⚙️ Управление MPU6050</h2>
            
            <div class="btn-group">
                <button id="startStreamBtn" class="btn" disabled>
                    <span>📊</span> Запустить поток данных
                </button>
                <button id="stopStreamBtn" class="btn" disabled>
                    <span>⏸️</span> Остановить поток
                </button>
                <button id="getDataBtn" class="btn" disabled>
                    <span>📈</span> Получить данные
                </button>
            </div>
            
            <div class="zero-controls">
                <h3>🎯 Управление нулевой точкой</h3>
                <div class="btn-group">
                    <button id="setZeroBtn" class="btn btn-success" disabled>
                        <span>🎯</span> Установить ноль
                    </button>
                    <button id="resetZeroBtn" class="btn btn-warning" disabled>
                        <span>🔄</span> Сбросить ноль
                    </button>
                    <button id="resetAnglesBtn" class="btn btn-danger" disabled>
                        <span>🗑️</span> Сбросить углы
                    </button>
                </div>
                <div style="margin-top: 15px; padding: 10px; background: white; border-radius: 5px;">
                    <div style="font-size: 14px; color: #666;">
                        <strong>Нулевая точка:</strong> <span id="zeroStatus" style="color: #dc3545; font-weight: bold;">Не установлена</span>
                    </div>
                </div>
            </div>
            
            <div class="data-display" id="dataDisplay">
                <div class="data-card">
                    <h3>Pitch (Тангаж)</h3>
                    <div class="data-value" id="pitchValue">0.0°</div>
                    <div class="data-label">Угол наклона вперед/назад</div>
                </div>
                <div class="data-card">
                    <h3>Roll (Крен)</h3>
                    <div class="data-value" id="rollValue">0.0°</div>
                    <div class="data-label">Угол наклона влево/вправо</div>
                </div>
                <div class="data-card">
                    <h3>Yaw (Рыскание)</h3>
                    <div class="data-value" id="yawValue">0.0°</div>
                    <div class="data-label">Угол поворота вокруг вертикали</div>
                </div>
            </div>
            
            <div class="visualization">
                <h3 style="text-align: center; margin-bottom: 20px;">3D Визуализация ориентации</h3>
                <div class="cube-container">
                    <div class="cube" id="cube">
                        <div class="face front">FRONT</div>
                        <div class="face back">BACK</div>
                        <div class="face right">RIGHT</div>
                        <div class="face left">LEFT</div>
                        <div class="face top">TOP</div>
                        <div class="face bottom">BOTTOM</div>
                    </div>
                </div>
            </div>
            
            <div class="sensor-container">
                <h3 style="color: white; text-align: center;">📊 Дополнительные данные</h3>
                <div class="sensor-grid">
                    <div class="sensor-item">
                        <div class="data-label" style="color: rgba(255,255,255,0.8)">Относительный Pitch</div>
                        <div class="sensor-value" id="relPitchValue">0.0°</div>
                    </div>
                    <div class="sensor-item">
                        <div class="data-label" style="color: rgba(255,255,255,0.8)">Относительный Roll</div>
                        <div class="sensor-value" id="relRollValue">0.0°</div>
                    </div>
                    <div class="sensor-item">
                        <div class="data-label" style="color: rgba(255,255,255,0.8)">Относительный Yaw</div>
                        <div class="sensor-value" id="relYawValue">0.0°</div>
                    </div>
                    <div class="sensor-item">
                        <div class="data-label" style="color: rgba(255,255,255,0.8)">Накопленный Pitch</div>
                        <div class="sensor-value" id="accPitchValue">0.0°</div>
                    </div>
                    <div class="sensor-item">
                        <div class="data-label" style="color: rgba(255,255,255,0.8)">Накопленный Roll</div>
                        <div class="sensor-value" id="accRollValue">0.0°</div>
                    </div>
                    <div class="sensor-item">
                        <div class="data-label" style="color: rgba(255,255,255,0.8)">Накопленный Yaw</div>
                        <div class="sensor-value" id="accYawValue">0.0°</div>
                    </div>
                </div>
            </div>
            
            <div class="quick-commands">
                <h3>🚀 Быстрые команды:</h3>
                <div class="btn-group">
                    <button class="btn quick-btn" data-command="RECALIBRATE">
                        <span>🔧</span> Рекалибровка
                    </button>
                    <button class="btn quick-btn" data-command="SCAN_I2C">
                        <span>🔍</span> Сканировать I2C
                    </button>
                    <button class="btn quick-btn" data-command="LED ON">
                        <span>💡</span> LED ВКЛ
                    </button>
                    <button class="btn quick-btn" data-command="LED OFF">
                        <span>🔦</span> LED ВЫКЛ
                    </button>
                    <button class="btn quick-btn" data-command="STATUS">
                        <span>📊</span> Статус
                    </button>
                </div>
            </div>
        </div>
        
        <div class="log-container">
            <div class="log-title">
                <span>📝 Журнал событий:</span>
                <button id="clearLogBtn" class="btn" style="padding: 5px 10px; font-size: 12px;">
                    <span>🧹</span> Очистить лог
                </button>
            </div>
            <div class="log" id="logContent">
                <!-- Лог будет добавляться здесь -->
            </div>
        </div>
    </div>

    <script>
        // Конфигурация BLE
        const BLE_CONFIG = {
            SERVICE_UUID: '4fafc201-1fb5-459e-8fcc-c5c9c331914b',
            CHARACTERISTIC_UUID: 'beb5483e-36e1-4688-b7f5-ea07361b26a8',
            DEVICE_NAME: 'ESP32_MPU6050_BLE'
        };
        
        // Состояние приложения
        let state = {
            device: null,
            server: null,
            service: null,
            characteristic: null,
            isConnected: false,
            isStreaming: false,
            logEntries: 0,
            lastDataTime: 0,
            dataCount: 0
        };
        
        // DOM элементы
        const elements = {
            status: document.getElementById('status'),
            scanBtn: document.getElementById('scanBtn'),
            connectBtn: document.getElementById('connectBtn'),
            disconnectBtn: document.getElementById('disconnectBtn'),
            startStreamBtn: document.getElementById('startStreamBtn'),
            stopStreamBtn: document.getElementById('stopStreamBtn'),
            getDataBtn: document.getElementById('getDataBtn'),
            setZeroBtn: document.getElementById('setZeroBtn'),
            resetZeroBtn: document.getElementById('resetZeroBtn'),
            resetAnglesBtn: document.getElementById('resetAnglesBtn'),
            logContent: document.getElementById('logContent'),
            logCount: document.getElementById('logCount'),
            deviceInfo: document.getElementById('deviceInfo'),
            deviceDetails: document.getElementById('deviceDetails'),
            clearLogBtn: document.getElementById('clearLogBtn'),
            pitchValue: document.getElementById('pitchValue'),
            rollValue: document.getElementById('rollValue'),
            yawValue: document.getElementById('yawValue'),
            relPitchValue: document.getElementById('relPitchValue'),
            relRollValue: document.getElementById('relRollValue'),
            relYawValue: document.getElementById('relYawValue'),
            accPitchValue: document.getElementById('accPitchValue'),
            accRollValue: document.getElementById('accRollValue'),
            accYawValue: document.getElementById('accYawValue'),
            zeroStatus: document.getElementById('zeroStatus'),
            cube: document.getElementById('cube')
        };
        
        // Утилиты
        const utils = {
            getTimeString: () => {
                const now = new Date();
                return now.toLocaleTimeString('ru-RU', {
                    hour: '2-digit',
                    minute: '2-digit',
                    second: '2-digit'
                });
            },
            
            formatNumber: (num, decimals = 1) => {
                return parseFloat(num).toFixed(decimals);
            },
            
            updateLogCount: () => {
                if (elements.logCount) {
                    elements.logCount.textContent = `${state.logEntries} сообщений`;
                }
            },
            
            parseSensorData: (dataString) => {
                const data = {};
                const pairs = dataString.split(',');
                pairs.forEach(pair => {
                    const [key, value] = pair.split(':');
                    if (key && value !== undefined) {
                        data[key] = value;
                    }
                });
                return data;
            }
        };
        
        // Функции логирования
        const logger = {
            add: (message, type = 'info') => {
                const entry = document.createElement('div');
                entry.className = `log-entry log-${type}`;
                
                const timeSpan = document.createElement('span');
                timeSpan.className = 'log-time';
                timeSpan.textContent = utils.getTimeString();
                
                const messageSpan = document.createElement('span');
                messageSpan.className = 'log-message';
                messageSpan.textContent = message;
                
                entry.appendChild(timeSpan);
                entry.appendChild(messageSpan);
                elements.logContent.appendChild(entry);
                
                // Прокрутка вниз
                elements.logContent.scrollTop = elements.logContent.scrollHeight;
                
                // Обновление счетчика
                state.logEntries++;
                utils.updateLogCount();
            },
            
            clear: () => {
                elements.logContent.innerHTML = '';
                state.logEntries = 0;
                utils.updateLogCount();
                logger.add('Журнал очищен', 'info');
            },
            
            error: (message) => logger.add(`❌ ${message}`, 'error'),
            info: (message) => logger.add(`ℹ️ ${message}`, 'info'),
            sent: (message) => logger.add(`📤 ${message}`, 'sent'),
            received: (message) => logger.add(`📥 ${message}`, 'received')
        };
        
        // Функции управления состоянием
        const statusManager = {
            set: (message, type) => {
                const indicator = elements.status.querySelector('.indicator');
                if (indicator) {
                    indicator.className = 'indicator ' + (type === 'connected' ? 'online' : 'offline');
                }
                elements.status.textContent = message;
                elements.status.className = `status ${type}`;
                
                // Обновляем состояние кнопок
                const isConnected = type === 'connected';
                const isScanning = type === 'scanning';
                
                elements.scanBtn.disabled = isScanning || isConnected;
                elements.connectBtn.disabled = !state.device || isConnected || isScanning;
                elements.disconnectBtn.disabled = !isConnected;
                elements.startStreamBtn.disabled = !isConnected;
                elements.stopStreamBtn.disabled = !isConnected || !state.isStreaming;
                elements.getDataBtn.disabled = !isConnected;
                elements.setZeroBtn.disabled = !isConnected;
                elements.resetZeroBtn.disabled = !isConnected;
                elements.resetAnglesBtn.disabled = !isConnected;
                
                // Показываем/скрываем информацию об устройстве
                if (isConnected && state.device) {
                    elements.deviceInfo.style.display = 'block';
                    elements.deviceDetails.innerHTML = `
                        <div><strong>Имя:</strong> ${state.device.name || 'ESP32_MPU6050_BLE'}</div>
                        <div><strong>ID:</strong> ${state.device.id}</div>
                        <div><strong>Статус:</strong> Подключено</div>
                        <div><strong>Поток данных:</strong> ${state.isStreaming ? 'Активен' : 'Остановлен'}</div>
                    `;
                } else {
                    elements.deviceInfo.style.display = 'none';
                }
            },
            
            updateStreamingStatus: () => {
                elements.startStreamBtn.disabled = state.isStreaming;
                elements.stopStreamBtn.disabled = !state.isStreaming;
            }
        };
        
        // BLE функции
        const ble = {
            scan: async () => {
                statusManager.set('🔍 Сканирование BLE устройств...', 'scanning');
                logger.info('Начинаю сканирование BLE устройств...');
                
                try {
                    const options = {
                        filters: [{ name: BLE_CONFIG.DEVICE_NAME }],
                        optionalServices: [BLE_CONFIG.SERVICE_UUID]
                    };
                    
                    state.device = await navigator.bluetooth.requestDevice(options);
                    
                    if (!state.device) {
                        throw new Error('Устройство не выбрано');
                    }
                    
                    logger.info(`Найдено устройство: ${state.device.name || BLE_CONFIG.DEVICE_NAME}`);
                    statusManager.set(`✅ Найдено: ${state.device.name || BLE_CONFIG.DEVICE_NAME}`, 'disconnected');
                    
                } catch (error) {
                    logger.error(`Сканирование: ${error.message}`);
                    statusManager.set('❌ Ошибка сканирования', 'disconnected');
                }
            },
            
            connect: async () => {
                if (!state.device) {
                    logger.error('Сначала выберите устройство');
                    return;
                }
                
                statusManager.set('🔄 Подключение к устройству...', 'scanning');
                logger.info(`Подключаюсь к ${state.device.name || BLE_CONFIG.DEVICE_NAME}...`);
                
                try {
                    state.server = await state.device.gatt.connect();
                    logger.info('GATT сервер подключен');
                    
                    state.service = await state.server.getPrimaryService(BLE_CONFIG.SERVICE_UUID);
                    logger.info(`Служба найдена: ${BLE_CONFIG.SERVICE_UUID}`);
                    
                    state.characteristic = await state.service.getCharacteristic(BLE_CONFIG.CHARACTERISTIC_UUID);
                    logger.info(`Характеристика найдена: ${BLE_CONFIG.CHARACTERISTIC_UUID}`);
                    
                    // Подписываемся на уведомления
                    await state.characteristic.startNotifications();
                    state.characteristic.addEventListener('characteristicvaluechanged', ble.handleNotification);
                    
                    state.isConnected = true;
                    statusManager.set(`✅ Подключено к: ${state.device.name || BLE_CONFIG.DEVICE_NAME}`, 'connected');
                    logger.info('Успешно подключено к устройству!');
                    
                    // Получаем начальные данные
                    setTimeout(() => {
                        ble.sendCommand('GET_DATA');
                        ble.sendCommand('STATUS');
                    }, 500);
                    
                } catch (error) {
                    logger.error(`Подключение: ${error.message}`);
                    statusManager.set('❌ Ошибка подключения', 'disconnected');
                    state.isConnected = false;
                }
            },
            
            handleNotification: (event) => {
                const decoder = new TextDecoder();
                const dataString = decoder.decode(event.target.value);
                
                logger.received(dataString);
                
                // Обработка данных сенсора
                if (dataString.includes('PITCH:') && dataString.includes('ROLL:') && dataString.includes('YAW:')) {
                    const data = utils.parseSensorData(dataString);
                    
                    // Обновляем значения на экране
                    if (data.PITCH) {
                        elements.pitchValue.textContent = `${utils.formatNumber(data.PITCH)}°`;
                        elements.relPitchValue.textContent = `${utils.formatNumber(data.REL_PITCH || 0)}°`;
                        elements.accPitchValue.textContent = `${utils.formatNumber(data.ACC_PITCH || 0)}°`;
                    }
                    if (data.ROLL) {
                        elements.rollValue.textContent = `${utils.formatNumber(data.ROLL)}°`;
                        elements.relRollValue.textContent = `${utils.formatNumber(data.REL_ROLL || 0)}°`;
                        elements.accRollValue.textContent = `${utils.formatNumber(data.ACC_ROLL || 0)}°`;
                    }
                    if (data.YAW) {
                        elements.yawValue.textContent = `${utils.formatNumber(data.YAW)}°`;
                        elements.relYawValue.textContent = `${utils.formatNumber(data.REL_YAW || 0)}°`;
                        elements.accYawValue.textContent = `${utils.formatNumber(data.ACC_YAW || 0)}°`;
                    }
                    if (data.ZERO_SET) {
                        const isZeroSet = data.ZERO_SET === 'true';
                        elements.zeroStatus.textContent = isZeroSet ? 'Установлена' : 'Не установлена';
                        elements.zeroStatus.style.color = isZeroSet ? '#28a745' : '#dc3545';
                    }
                    
                    // Обновляем 3D визуализацию
                    if (data.PITCH && data.ROLL && data.YAW) {
                        elements.cube.style.transform = 
                            `rotateX(${data.ROLL}deg) rotateY(${data.YAW}deg) rotateZ(${data.PITCH}deg)`;
                    }
                }
                
                // Обработка специальных сообщений
                if (dataString === 'ZERO_POINT_SET') {
                    elements.zeroStatus.textContent = 'Установлена';
                    elements.zeroStatus.style.color = '#28a745';
                    logger.info('Нулевая точка установлена');
                }
                if (dataString === 'ZERO_POINT_RESET') {
                    elements.zeroStatus.textContent = 'Не установлена';
                    elements.zeroStatus.style.color = '#dc3545';
                    logger.info('Нулевая точка сброшена');
                }
                if (dataString === 'ANGLES_RESET') {
                    logger.info('Углы сброшены');
                }
                if (dataString === 'RECALIBRATION_COMPLETE') {
                    logger.info('Рекалибровка завершена');
                }
                if (dataString.startsWith('I2C_SCAN_COMPLETE')) {
                    logger.info('Сканирование I2C завершено');
                }
                if (dataString.startsWith('STATUS:')) {
                    logger.info(`Статус устройства: ${dataString}`);
                }
            },
            
            disconnect: async () => {
                if (!state.device || !state.isConnected) {
                    return;
                }
                
                try {
                    state.isStreaming = false;
                    
                    if (state.characteristic) {
                        await state.characteristic.stopNotifications();
                        state.characteristic.removeEventListener('characteristicvaluechanged', ble.handleNotification);
                    }
                    
                    if (state.device.gatt.connected) {
                        state.device.gatt.disconnect();
                    }
                    
                    state.isConnected = false;
                    statusManager.set('🔌 Отключено от устройства', 'disconnected');
                    logger.info('Отключено от устройства');
                    
                } catch (error) {
                    logger.error(`Отключение: ${error.message}`);
                }
            },
            
            sendCommand: async (command) => {
                if (!state.isConnected || !state.characteristic) {
                    logger.error('Не подключено к устройству');
                    return;
                }
                
                command = command.trim();
                if (!command) {
                    logger.error('Пустая команда');
                    return;
                }
                
                try {
                    const encoder = new TextEncoder();
                    await state.characteristic.writeValue(encoder.encode(command));
                    logger.sent(`Команда: "${command}"`);
                    
                } catch (error) {
                    logger.error(`Отправка команды: ${error.message}`);
                }
            }
        };
        
        // Настройка обработчиков событий
        function setupEventListeners() {
            elements.scanBtn.addEventListener('click', ble.scan);
            elements.connectBtn.addEventListener('click', ble.connect);
            elements.disconnectBtn.addEventListener('click', ble.disconnect);
            elements.clearLogBtn.addEventListener('click', logger.clear);
            
            elements.startStreamBtn.addEventListener('click', () => {
                state.isStreaming = true;
                statusManager.updateStreamingStatus();
                ble.sendCommand('START_STREAM');
                logger.info('Поток данных запущен');
            });
            
            elements.stopStreamBtn.addEventListener('click', () => {
                state.isStreaming = false;
                statusManager.updateStreamingStatus();
                ble.sendCommand('STOP_STREAM');
                logger.info('Поток данных остановлен');
            });
            
            elements.getDataBtn.addEventListener('click', () => {
                ble.sendCommand('GET_DATA');
            });
            
            elements.setZeroBtn.addEventListener('click', () => {
                ble.sendCommand('SET_ZERO');
            });
            
            elements.resetZeroBtn.addEventListener('click', () => {
                ble.sendCommand('RESET_ZERO');
            });
            
            elements.resetAnglesBtn.addEventListener('click', () => {
                ble.sendCommand('RESET_ANGLES');
            });
            
            // Быстрые команды
            document.querySelectorAll('[data-command]').forEach(button => {
                button.addEventListener('click', () => {
                    const command = button.getAttribute('data-command');
                    ble.sendCommand(command);
                });
            });
        }
        
        // Инициализация приложения
        function initApp() {
            logger.info('Веб-приложение для управления ESP32 MPU6050 через BLE загружено');
            
            if (!navigator.bluetooth) {
                logger.error('Web Bluetooth API не поддерживается в этом браузере');
                logger.error('Используйте Chrome/Edge на Windows/Mac/Android');
                elements.scanBtn.disabled = true;
                elements.connectBtn.disabled = true;
                statusManager.set('❌ Браузер не поддерживает Bluetooth', 'disconnected');
                return;
            }
            
            logger.info('Готов к работе. Нажмите "Сканировать BLE устройства"');
            setupEventListeners();
        }
        
        window.addEventListener('load', initApp);
    </script>
</body>
</html>
)rawliteral";
  
  return html;
}

void setup() {
  Serial.begin(115200);
  
  // Настройка встроенного LED
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);
  
  Serial.println("Starting ESP32 with MPU6050 and BLE...");
  
  // Инициализация I2C
  Wire.begin();
  
  // Сканирование I2C шины
  scanI2C();
  
  // Инициализация MPU6050
  if (mpuFound) {
    if (!mpu.begin(mpuAddress, &Wire)) {
      Serial.println("Failed to initialize MPU6050!");
      mpuFound = false;
    }
  } else {
    // Попытка использовать адрес по умолчанию
    if (!mpu.begin()) {
      Serial.println("MPU6050 not found at default address!");
      mpuFound = false;
    } else {
      mpuFound = true;
      Serial.println("MPU6050 found at default address!");
    }
  }
  
  if (mpuFound) {
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setGyroRange(MPU6050_RANGE_250_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
    
    calibrateSensor();
  }
  
  // Загрузка HTML страницы
  htmlPage = loadHTMLFromSPIFFS();
  if (htmlPage == "") {
    Serial.println("Using built-in HTML page");
    htmlPage = createHTMLPage();
  }
  
  // Инициализация BLE
  BLEDevice::init(DEVICE_NAME);
  
  // Создание сервера
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  // Создание службы
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  // Создание характеристики
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->setValue("ESP32 MPU6050 BLE Ready");
  
  // Добавляем дескриптор для уведомлений
  pCharacteristic->addDescriptor(new BLE2902());
  
  // Запуск службы
  pService->start();
  
  // Настройка рекламы
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // Рекомендуется для iOS
  pAdvertising->setMinPreferred(0x12);
  
  // Запуск рекламы
  BLEDevice::startAdvertising();
  
  Serial.println("BLE Server ready!");
  Serial.println("Device name: " + String(DEVICE_NAME));
  Serial.println("Service UUID: " + String(SERVICE_UUID));
  Serial.println("Characteristic UUID: " + String(CHARACTERISTIC_UUID));
  Serial.println("MPU6050: " + String(mpuFound ? "Found" : "Not found"));
  Serial.println("Waiting for BLE connections...");
}

void loop() {
  if (!mpuFound || !calibrated) {
    delay(100);
    return;
  }
  
  // Обработка подключения/отключения BLE
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // Даем время для завершения соединения
    pServer->startAdvertising(); // Перезапускаем рекламу
    Serial.println("Start advertising");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  // Чтение данных с MPU6050
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  if (lastTime == 0) deltaTime = 0.01;
  lastTime = currentTime;
  
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  float accelPitch = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
  float accelRoll = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
  
  pitch += gyroX * deltaTime * 180.0 / PI;
  roll += gyroY * deltaTime * 180.0 / PI;
  yaw += gyroZ * deltaTime * 180.0 / PI;
  
  float alpha = 0.96;
  pitch = alpha * pitch + (1.0 - alpha) * accelPitch;
  roll = alpha * roll + (1.0 - alpha) * accelRoll;
  
  // Автоматическая отправка данных при включенном потоке
  if (deviceConnected && shouldSendData) {
    if (dataChanged() || (currentTime - lastDataSend >= SEND_INTERVAL)) {
      sendSensorData();
    }
  }
  
  delay(10);
}
