#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>

// UUID для службы и характеристики
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Настройки MPU6050
#define MPU_ADDR 0x68
#define SDA_PIN 21
#define SCL_PIN 22

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Структура для данных с датчика - УПРОЩЕННАЯ версия
struct SensorData {
  float ax, ay, az;     // Ускорение (g)
  float gx, gy, gz;     // Гироскоп (°/s)
  float temperature;    // Температура (°C)
  unsigned long uptime; // Время работы (сек)
} sensorData;

// ПЕРЕНОС ВСЕХ ПЕРЕМЕННЫХ И ФУНКЦИЙ ИЗ WiFi ВЕРСИИ ========================

// Данные ориентации
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

// Настройки отправки данных
unsigned long lastDataSend = 0;
const unsigned long SEND_INTERVAL = 50;
const float CHANGE_THRESHOLD = 1.0;

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

// Калибровка сенсора (аналогично WiFi версии)
void calibrateSensor() {
  Serial.println("Calibrating...");
  float sumX = 0, sumY = 0, sumZ = 0;
  
  // Используем низкоуровневое чтение вместо библиотеки
  for (int i = 0; i < 500; i++) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x43); // регистр GYRO_XOUT_H
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, 6, true);
    
    if (Wire.available() >= 6) {
      int16_t gx_raw = Wire.read() << 8 | Wire.read();
      int16_t gy_raw = Wire.read() << 8 | Wire.read();
      int16_t gz_raw = Wire.read() << 8 | Wire.read();
      
      // Конвертация в °/s (±250 °/s диапазон -> 131 LSB/°/s)
      sumX += gx_raw / 131.0;
      sumY += gy_raw / 131.0;
      sumZ += gz_raw / 131.0;
    }
    delay(2);
  }
  
  gyroOffsetX = sumX / 500;
  gyroOffsetY = sumY / 500;
  gyroOffsetZ = sumZ / 500;
  calibrated = true;
  
  Serial.println("Calibration complete");
  Serial.print("Offsets - X: "); Serial.print(gyroOffsetX, 4);
  Serial.print(" Y: "); Serial.print(gyroOffsetY, 4);
  Serial.print(" Z: "); Serial.println(gyroOffsetZ, 4);
}

// Формирование строки с данными для отправки
String getSensorDataString() {
  // Обновляем накопленные углы
  updateAccumulatedAngles();
  
  // Получаем относительные углы
  double relPitch = getRelativePitch();
  double relRoll = getRelativeRoll();
  double relYaw = getRelativeYaw();
  
  // Формируем строку аналогично WiFi версии
  String data = "PITCH:" + String(pitch, 1) + 
                ",ROLL:" + String(roll, 1) + 
                ",YAW:" + String(yaw, 1) +
                ",REL_PITCH:" + String(relPitch, 2) +
                ",REL_ROLL:" + String(relRoll, 2) +
                ",REL_YAW:" + String(relYaw, 2) +
                ",ACC_PITCH:" + String(accumulatedPitch, 2) +
                ",ACC_ROLL:" + String(accumulatedRoll, 2) +
                ",ACC_YAW:" + String(accumulatedYaw, 2) +
                ",ZERO_SET:" + String(zeroSet ? "true" : "false");
  
  return data;
}

// Проверка изменения данных для отправки
bool dataChanged() {
  return (abs(pitch - lastSentPitch) >= CHANGE_THRESHOLD ||
          abs(roll - lastSentRoll) >= CHANGE_THRESHOLD ||
          abs(yaw - lastSentYaw) >= CHANGE_THRESHOLD);
}

// Расчет углов по акселерометру и гироскопу (фильтр Калмана)
void calculateAngles() {
  if (!calibrated) return;
  
  // Чтение сырых данных
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); // starting with register 0x3B (ACCEL_XOUT_H)
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 14, true);
  
  if (Wire.available() >= 14) {
    // Чтение акселерометра
    int16_t ax_raw = Wire.read() << 8 | Wire.read();
    int16_t ay_raw = Wire.read() << 8 | Wire.read();
    int16_t az_raw = Wire.read() << 8 | Wire.read();
    
    // Чтение температуры
    Wire.read() << 8 | Wire.read(); // пропускаем температуру
    
    // Чтение гироскопа
    int16_t gx_raw = Wire.read() << 8 | Wire.read();
    int16_t gy_raw = Wire.read() << 8 | Wire.read();
    int16_t gz_raw = Wire.read() << 8 | Wire.read();
    
    // Конвертация в физические величины
    // ±2g диапазон -> 16384 LSB/g
    float ax = ax_raw / 16384.0;
    float ay = ay_raw / 16384.0;
    float az = az_raw / 16384.0;
    
    // ±250 °/s диапазон -> 131 LSB/°/s
    float gx = gx_raw / 131.0 - gyroOffsetX;
    float gy = gy_raw / 131.0 - gyroOffsetY;
    float gz = gz_raw / 131.0 - gyroOffsetZ;
    
    // Расчет времени
    unsigned long currentTime = millis();
    float deltaTime = (currentTime - lastTime) / 1000.0;
    if (lastTime == 0) deltaTime = 0.01;
    lastTime = currentTime;
    
    // Углы по акселерометру
    float accelPitch = atan2(ay, az) * 180.0 / PI;
    float accelRoll = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
    
    // Интегрирование гироскопа
    pitch += gx * deltaTime * 180.0 / PI;
    roll += gy * deltaTime * 180.0 / PI;
    yaw += gz * deltaTime * 180.0 / PI;
    
    // Комплементарный фильтр (аналогично WiFi версии)
    float alpha = 0.96;
    pitch = alpha * pitch + (1.0 - alpha) * accelPitch;
    roll = alpha * roll + (1.0 - alpha) * accelRoll;
    
    // Сохраняем также в структуру sensorData для обратной совместимости
    sensorData.ax = ax;
    sensorData.ay = ay;
    sensorData.az = az;
    sensorData.gx = gx;
    sensorData.gy = gy;
    sensorData.gz = gz;
  }
}

// =========================================================================

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Устройство подключено");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Устройство отключено");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      String value = pCharacteristic->getValue();
      
      if (value.length() > 0) {
        Serial.print("Получено: ");
        Serial.println(value);
        
        String response = "";
        
        // Обработка команд (расширенный набор из WiFi версии)
        if (value == "LED ON") {
          response = "LED включен";
          digitalWrite(2, HIGH);
        } 
        else if (value == "LED OFF") {
          response = "LED выключен";
          digitalWrite(2, LOW);
        } 
        else if (value == "STATUS") {
          response = "Статус: OK, Uptime: " + String(millis() / 1000) + "s, Calibrated: " + (calibrated ? "Yes" : "No");
        } 
        else if (value == "TEMP") {
          response = "Температура: " + String(sensorData.temperature, 2) + "°C";
        }
        else if (value == "DATA") {
          // Полные данные с датчика
          response = String(sensorData.ax, 2) + "," + 
                     String(sensorData.ay, 2) + "," + 
                     String(sensorData.az, 2) + "," + 
                     String(sensorData.gx, 2) + "," + 
                     String(sensorData.gy, 2) + "," + 
                     String(sensorData.gz, 2) + "," + 
                     String(sensorData.temperature, 2) + "," + 
                     String(sensorData.uptime);
        }
        else if (value == "ACCEL") {
          // Только данные акселерометра
          response = String(sensorData.ax, 2) + "," + 
                     String(sensorData.ay, 2) + "," + 
                     String(sensorData.az, 2);
        }
        else if (value == "GYRO") {
          // Только данные гироскопа
          response = String(sensorData.gx, 2) + "," + 
                     String(sensorData.gy, 2) + "," + 
                     String(sensorData.gz, 2);
        }
        else if (value == "GET_DATA") {
          // ОРИЕНТАЦИЯ (аналогично WiFi версии)
          response = getSensorDataString();
        }
        else if (value == "RECALIBRATE") {
          calibrated = false;
          calibrateSensor();
          response = "RECALIBRATION_COMPLETE";
        }
        else if (value == "RESET_ANGLES") {
          pitch = 0; roll = 0; yaw = 0;
          lastSentPitch = 0; lastSentRoll = 0; lastSentYaw = 0;
          resetZeroPoint();
          response = "ANGLES_RESET";
        }
        else if (value == "SET_ZERO") {
          setZeroPoint();
          response = "ZERO_POINT_SET";
        }
        else if (value == "RESET_ZERO") {
          resetZeroPoint();
          response = "ZERO_POINT_RESET";
        }
        else if (value == "RESTART") {
          response = "Перезагрузка...";
          pCharacteristic->setValue(response.c_str());
          pCharacteristic->notify();
          delay(100);
          ESP.restart();
        } 
        else {
          response = "Echo: " + value;
        }
        
        // Отправляем ответ
        pCharacteristic->setValue(response.c_str());
        pCharacteristic->notify();
      }
    }
};

// Инициализация MPU6050
void initMPU6050() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); // PWR_MGMT_1 register
  Wire.write(0x00); // выход из sleep режима
  Wire.endTransmission(true);
  
  // Настройка гироскопа ±250 °/s
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B); // GYRO_CONFIG register
  Wire.write(0x00); 
  Wire.endTransmission(true);
  
  // Настройка акселерометра ±2g
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C); // ACCEL_CONFIG register
  Wire.write(0x00); 
  Wire.endTransmission(true);
  
  // Настройка фильтра
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1A); // CONFIG register
  Wire.write(0x03); // DLPF_CFG = 3
  Wire.endTransmission(true);
}

void setup() {
  Serial.begin(115200);
  
  // Настройка встроенного LED
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);
  
  // Инициализация I2C для MPU6050
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  initMPU6050();
  
  // КАЛИБРАЦИЯ (как в WiFi версии)
  calibrateSensor();
  
  Serial.println("Запуск BLE сервера с MPU6050...");
  Serial.println("Используется алгоритм ориентации из WiFi версии");

  // Инициализация BLE
  BLEDevice::init("ESP32_MPU6050");
  
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
  pCharacteristic->setValue("ESP32 MPU6050 Ready");
  
  // Добавляем дескриптор для уведомлений
  pCharacteristic->addDescriptor(new BLE2902());

  // Запуск службы
  pService->start();

  // Настройка рекламы
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  
  // Запуск рекламы
  BLEDevice::startAdvertising();
  
  Serial.println("BLE сервер готов!");
  Serial.println("Имя устройства: ESP32_MPU6050");
  Serial.println("UUID службы: " + String(SERVICE_UUID));
  Serial.println("UUID характеристики: " + String(CHARACTERISTIC_UUID));
  Serial.println("Ожидание подключений...");
  Serial.println("\nДоступные команды:");
  Serial.println("  LED ON/OFF - управление светодиодом");
  Serial.println("  STATUS - информация о системе");
  Serial.println("  TEMP - температура датчика");
  Serial.println("  DATA - все сырые данные с датчика");
  Serial.println("  ACCEL - только акселерометр");
  Serial.println("  GYRO - только гироскоп");
  Serial.println("  GET_DATA - углы ориентации (PITCH,ROLL,YAW)");
  Serial.println("  RECALIBRATE - перекалибровка");
  Serial.println("  RESET_ANGLES - сброс всех углов");
  Serial.println("  SET_ZERO - установить относительный ноль");
  Serial.println("  RESET_ZERO - сбросить относительный ноль");
  Serial.println("  RESTART - перезагрузка ESP32");
}

void loop() {
  // Расчет углов ориентации
  calculateAngles();
  
  // Автоматическая отправка данных при подключении (аналогично WiFi версии)
  if (deviceConnected) {
    unsigned long currentTime = millis();
    
    if (currentTime - lastDataSend >= SEND_INTERVAL) {
      if (dataChanged() || lastDataSend == 0) {
        String data = getSensorDataString();
        pCharacteristic->setValue(data.c_str());
        pCharacteristic->notify();
        
        lastSentPitch = pitch;
        lastSentRoll = roll;
        lastSentYaw = yaw;
        lastDataSend = currentTime;
        
        // Вывод в Serial для отладки
        static unsigned long lastSerialPrint = 0;
        if (currentTime - lastSerialPrint > 1000) {
          Serial.print("Pitch: "); Serial.print(pitch, 1);
          Serial.print("°, Roll: "); Serial.print(roll, 1);
          Serial.print("°, Yaw: "); Serial.print(yaw, 1); Serial.println("°");
          
          Serial.print("Rel Pitch: "); Serial.print(getRelativePitch(), 1);
          Serial.print("°, Rel Roll: "); Serial.print(getRelativeRoll(), 1);
          Serial.print("°, Rel Yaw: "); Serial.print(getRelativeYaw(), 1); Serial.println("°");
          
          lastSerialPrint = currentTime;
        }
      }
    }
  }
  
  // Обработка отключения/переподключения
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Начато рекламирование...");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  delay(10); // Небольшая задержка для стабильности
}
