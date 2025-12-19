/*
  MPU6050 Head Tracker - Serial Version
  Для работы через COM порт без WiFi и PCA9548A
  Отправка данных в формате JSON через Serial
*/

#include <Wire.h>
#include <Adafruit_MPU6050.h>

// MPU6050 датчик подключен напрямую к I2C
Adafruit_MPU6050 mpu;
bool mpuConnected = false;

// Переменные для данных сенсора
float pitch = 0, roll = 0, yaw = 0;
float smoothedPitch = 0, smoothedRoll = 0, smoothedYaw = 0;
const float smoothingFactor = 0.3;

// Переменные комплементарного фильтра
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
bool calibrated = false;
unsigned long lastTime = 0;
unsigned long calibrationStart = 0;
const unsigned long calibrationTime = 3000;

// Точка нуля (референсная позиция)
float zeroPitch = 0, zeroRoll = 0, zeroYaw = 0;
bool zeroSet = false;

// Автокалибровка
bool autoCalibrationEnabled = true;
const unsigned long AUTO_CALIBRATION_INTERVAL = 60000;
unsigned long lastAutoCalibration = 0;

// Управление отправкой данных
unsigned long lastDataSend = 0;
const unsigned long DATA_SEND_INTERVAL = 50; // 50ms между отправками

// Буфер для команд
String serialBuffer = "";

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10); // Ждем готовности Serial
  }
  
  // Инициализируем I2C
  Wire.begin();
  
  // Инициализируем MPU6050
  Serial.println("\n🔍 Инициализация MPU6050...");
  
  if (mpu.begin()) {
    mpuConnected = true;
    
    // Конфигурируем MPU6050 для отслеживания головы
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setGyroRange(MPU6050_RANGE_250_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
    
    Serial.println("✅ MPU6050 найден и инициализирован");
    Serial.println("\n🌐 Веб-интерфейс доступен по адресу:");
    Serial.println("   Откройте index_PCA9548A_3D.html в браузере");
    Serial.println("   и выберите 'COM Port' для подключения\n");
  } else {
    mpuConnected = false;
    Serial.println("❌ MPU6050 не найден!");
    Serial.println("   Проверьте подключение I2C:");
    Serial.println("   SDA -> A4, SCL -> A5 (Arduino Uno/Nano)");
    Serial.println("   SDA -> D2, SCL -> D1 (ESP8266)");
    Serial.println("   SDA -> 21, SCL -> 22 (ESP32)");
  }
  
  // Начинаем калибровку
  calibrationStart = millis();
  Serial.println("🔧 Калибруем гироскоп... Держите датчик неподвижно 3 секунды!");
}

void loop() {
  // Обрабатываем входящие команды из Serial
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        handleCommand(serialBuffer);
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }
  
  // Обрабатываем данные сенсора
  if (mpuConnected) {
    processSensorData();
  }
  
  // Проверяем автокалибровку
  checkAutoCalibration();
  
  // Отправляем данные через Serial
  unsigned long currentTime = millis();
  if (currentTime - lastDataSend >= DATA_SEND_INTERVAL) {
    sendSensorData(currentTime);
    lastDataSend = currentTime;
  }
  
  delay(10);
}

void processSensorData() {
  if (!calibrated) {
    calibrateGyro();
    return;
  }
  
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    Serial.println("Error reading MPU6050 data");
    return;
  }
  
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  if (lastTime == 0) {
    deltaTime = 0.01;
  }
  lastTime = currentTime;
  
  // Компенсируем смещение гироскопа
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  // Вычисляем углы из акселерометра
  float accelPitch = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
  float accelRoll = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
  
  // Интеграция гироскопа
  pitch += gyroX * deltaTime * 180.0 / PI;
  roll += gyroY * deltaTime * 180.0 / PI;
  yaw += gyroZ * deltaTime * 180.0 / PI;
  
  // Комплементарный фильтр для pitch и roll
  float alpha = 0.96;
  pitch = alpha * pitch + (1.0 - alpha) * accelPitch;
  roll = alpha * roll + (1.0 - alpha) * accelRoll;
  
  // Сглаживание для отображения
  smoothedPitch = smoothedPitch * (1 - smoothingFactor) + pitch * smoothingFactor;
  smoothedRoll = smoothedRoll * (1 - smoothingFactor) + roll * smoothingFactor;
  smoothedYaw = smoothedYaw * (1 - smoothingFactor) + yaw * smoothingFactor;
}

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
      Serial.print("Calibration progress: ");
      Serial.print(progress);
      Serial.println("%");
    }
  } else {
    gyroOffsetX = sumX / sampleCount;
    gyroOffsetY = sumY / sampleCount;
    gyroOffsetZ = sumZ / sampleCount;
    calibrated = true;
    
    Serial.println("✅ Калибровка гироскопа завершена!");
    Serial.print("Offsets - X:");
    Serial.print(gyroOffsetX, 6);
    Serial.print(", Y:");
    Serial.print(gyroOffsetY, 6);
    Serial.print(", Z:");
    Serial.println(gyroOffsetZ, 6);
    Serial.print("Обработано сэмплов: ");
    Serial.println(sampleCount);
    
    // Отправляем статус калибровки
    String statusMsg = "{\"type\":\"status\",\"message\":\"Calibration complete\"}";
    Serial.println(statusMsg);
  }
}

void checkAutoCalibration() {
  unsigned long currentTime = millis();
  
  if (mpuConnected && autoCalibrationEnabled && calibrated) {
    if (currentTime - lastAutoCalibration >= AUTO_CALIBRATION_INTERVAL) {
      // Проверяем, неподвижен ли сенсор для автокалибровки
      if (isSensorStationary()) {
        Serial.println("🔄 Автокалибровка...");
        performAutoCalibration();
        lastAutoCalibration = currentTime;
        
        // Отправляем уведомление
        String autoCalMsg = "{\"type\":\"autoCalibration\",\"message\":\"Auto-calibration performed\"}";
        Serial.println(autoCalMsg);
      }
    }
  }
}

bool isSensorStationary() {
  // Простая проверка: если значения гироскопа близки к нулю, сенсор вероятно неподвижен
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    return false;
  }
  
  float gyroX = abs(g.gyro.x - gyroOffsetX);
  float gyroY = abs(g.gyro.y - gyroOffsetY);
  float gyroZ = abs(g.gyro.z - gyroOffsetZ);
  
  // Порог для определения неподвижности (можно настраивать)
  float stationaryThreshold = 0.01;
  
  return (gyroX < stationaryThreshold && gyroY < stationaryThreshold && gyroZ < stationaryThreshold);
}

void performAutoCalibration() {
  // Быстрое обновление калибровки
  sensors_event_t a, g, temp;
  int samples = 20;
  float sumX = 0, sumY = 0, sumZ = 0;
  
  for (int i = 0; i < samples; i++) {
    mpu.getEvent(&a, &g, &temp);
    sumX += g.gyro.x;
    sumY += g.gyro.y;
    sumZ += g.gyro.z;
    delay(10);
  }
  
  // Обновляем смещения со сглаживанием
  float alpha = 0.3; // Коэффициент сглаживания для автокалибровки
  gyroOffsetX = gyroOffsetX * (1 - alpha) + (sumX / samples) * alpha;
  gyroOffsetY = gyroOffsetY * (1 - alpha) + (sumY / samples) * alpha;
  gyroOffsetZ = gyroOffsetZ * (1 - alpha) + (sumZ / samples) * alpha;
  
  Serial.print("🔄 Автокалибровка выполнена. Новые смещения - X:");
  Serial.print(gyroOffsetX, 6);
  Serial.print(", Y:");
  Serial.print(gyroOffsetY, 6);
  Serial.print(", Z:");
  Serial.println(gyroOffsetZ, 6);
}

void sendSensorData(unsigned long currentTime) {
  if (!mpuConnected || !calibrated) return;
  
  // Вычисляем относительные углы
  float relPitch = calculateRelativeAngle(smoothedPitch, zeroPitch);
  float relRoll = calculateRelativeAngle(smoothedRoll, zeroRoll);
  float relYaw = calculateRelativeAngle(smoothedYaw, zeroYaw);
  
  // Формируем JSON данные
  String json = "{";
  json += "\"type\":\"sensorData\",";
  json += "\"pitch\":" + String(relPitch, 2) + ",";
  json += "\"roll\":" + String(relRoll, 2) + ",";
  json += "\"yaw\":" + String(relYaw, 2) + ",";
  json += "\"absPitch\":" + String(smoothedPitch, 2) + ",";
  json += "\"absRoll\":" + String(smoothedRoll, 2) + ",";
  json += "\"absYaw\":" + String(smoothedYaw, 2) + ",";
  json += "\"zeroSet\":" + String(zeroSet ? "true" : "false") + ",";
  json += "\"calibrated\":" + String(calibrated ? "true" : "false") + ",";
  json += "\"autoCalibration\":" + String(autoCalibrationEnabled ? "true" : "false") + ",";
  json += "\"timestamp\":" + String(currentTime);
  json += "}";
  
  // Отправляем через Serial
  Serial.println(json);
  
  // Отладочный вывод каждые 2 секунды
  static unsigned long lastDebug = 0;
  if (currentTime - lastDebug >= 2000) {
    lastDebug = currentTime;
    
    Serial.print("📤 Данные: P:");
    Serial.print(smoothedPitch, 1);
    Serial.print("° R:");
    Serial.print(smoothedRoll, 1);
    Serial.print("° Y:");
    Serial.print(smoothedYaw, 1);
    Serial.print("° | REL P:");
    Serial.print(relPitch, 1);
    Serial.print("° R:");
    Serial.print(relRoll, 1);
    Serial.print("° Y:");
    Serial.print(relYaw, 1);
    Serial.print("° | AutoCal:");
    Serial.println(autoCalibrationEnabled ? "ON" : "OFF");
  }
}

void handleCommand(String command) {
  Serial.print("📨 Получена команда: ");
  Serial.println(command);
  
  // Разбираем JSON команду
  if (command.indexOf("setZero") != -1) {
    setZeroPoint();
    String response = "{\"type\":\"status\",\"message\":\"Zero point set\"}";
    Serial.println(response);
    
    // Отправляем информацию о точке нуля
    String zeroInfo = "{\"type\":\"zeroInfo\",\"zeroPitch\":" + String(zeroPitch, 2) + 
                     ",\"zeroRoll\":" + String(zeroRoll, 2) + 
                     ",\"zeroYaw\":" + String(zeroYaw, 2) + "}";
    Serial.println(zeroInfo);
  }
  else if (command.indexOf("resetZero") != -1) {
    resetZeroPoint();
    String response = "{\"type\":\"status\",\"message\":\"Zero point reset\"}";
    Serial.println(response);
  }
  else if (command.indexOf("recalibrate") != -1) {
    recalibrate();
    String response = "{\"type\":\"status\",\"message\":\"Recalibrating gyro...\"}";
    Serial.println(response);
  }
  else if (command.indexOf("resetYaw") != -1) {
    resetYaw();
    String response = "{\"type\":\"status\",\"message\":\"Yaw reset\"}";
    Serial.println(response);
  }
  else if (command.indexOf("setAutoCalibration") != -1) {
    // Парсим команду автокалибровки
    bool enable = true;
    
    // Извлекаем состояние вкл/выкл
    if (command.indexOf("\"enable\"") != -1) {
      int enableStart = command.indexOf("\"enable\":") + 9;
      int enableEnd = command.indexOf(",", enableStart);
      if (enableEnd == -1) enableEnd = command.indexOf("}", enableStart);
      String enableStr = command.substring(enableStart, enableEnd);
      enable = (enableStr == "true");
    }
    
    setAutoCalibration(enable);
    String response = "{\"type\":\"status\",\"message\":\"Auto-calibration " + String(enable ? "enabled" : "disabled") + "\"}";
    Serial.println(response);
    
    // Отправляем обновление статуса автокалибровки
    String autoCalUpdate = "{\"type\":\"autoCalibrationUpdate\",\"enabled\":" + String(enable ? "true" : "false") + "}";
    Serial.println(autoCalUpdate);
  }
  else if (command.indexOf("ping") != -1) {
    String pong = "{\"type\":\"pong\",\"timestamp\":" + String(millis()) + "}";
    Serial.println(pong);
  }
  else if (command.indexOf("getStatus") != -1) {
    // Отправляем полный статус
    String status = "{\"type\":\"fullStatus\",";
    status += "\"mpuConnected\":" + String(mpuConnected ? "true" : "false") + ",";
    status += "\"calibrated\":" + String(calibrated ? "true" : "false") + ",";
    status += "\"zeroSet\":" + String(zeroSet ? "true" : "false") + ",";
    status += "\"autoCalibration\":" + String(autoCalibrationEnabled ? "true" : "false") + ",";
    status += "\"timestamp\":" + String(millis());
    status += "}";
    Serial.println(status);
  }
}

float calculateRelativeAngle(float absoluteAngle, float zeroAngle) {
  float relative = absoluteAngle - zeroAngle;
  // Нормализуем от -180 до 180 градусов
  while (relative > 180) relative -= 360;
  while (relative < -180) relative += 360;
  return relative;
}

void setZeroPoint() {
  zeroPitch = smoothedPitch;
  zeroRoll = smoothedRoll;
  zeroYaw = smoothedYaw;
  zeroSet = true;
  
  Serial.print("💾 Точка нуля установлена - Pitch:");
  Serial.print(zeroPitch, 1);
  Serial.print("° Roll:");
  Serial.print(zeroRoll, 1);
  Serial.print("° Yaw:");
  Serial.print(zeroYaw, 1);
  Serial.println("°");
}

void resetZeroPoint() {
  zeroPitch = 0;
  zeroRoll = 0;
  zeroYaw = 0;
  zeroSet = false;
  
  Serial.println("🔄 Точка нуля сброшена");
}

void recalibrate() {
  calibrated = false;
  pitch = roll = yaw = 0;
  calibrationStart = millis();
  
  Serial.println("🔄 Перекалибровка начата");
}

void resetYaw() {
  yaw = 0;
  smoothedYaw = 0;
  
  Serial.println("🔄 Yaw сброшен");
}

void setAutoCalibration(bool enable) {
  autoCalibrationEnabled = enable;
  if (enable) {
    lastAutoCalibration = millis(); // Сбрасываем таймер при включении
  }
  
  Serial.print("⚙️ Автокалибровка ");
  Serial.println(enable ? "включена" : "выключена");
}