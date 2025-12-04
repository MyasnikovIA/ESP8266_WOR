// VR_Head_MPU6050_Bluetooth_Stable_FIXED_V2.ino
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>

// UUID для службы и характеристики
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Настройки MPU6050
#define MPU_ADDR 0x68
#define SDA_PIN 21
#define SCL_PIN 22

// Настройка светодиода
#define LED_PIN 2

// Константы для калибровки
#define CALIBRATION_SAMPLES 200
#define CALIBRATION_DELAY 5

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Структура для данных с датчика
struct SensorData {
  float ax, ay, az;     // Ускорение (g)
  float gx, gy, gz;     // Гироскоп (°/s)
  float temperature;    // Температура (°C)
  unsigned long uptime; // Время работы (сек)
} sensorData;

// Основные переменные для углов
float pitch = 0, roll = 0, yaw = 0;                      // Текущие углы (фильтрованные)
float displayPitch = 0, displayRoll = 0, displayYaw = 0; // Углы для отображения (нормализованные -180..180)
float lastSentPitch = 0, lastSentRoll = 0, lastSentYaw = 0;

// Смещения (калибровочные значения)
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
float accelOffsetX = 0, accelOffsetY = 0, accelOffsetZ = 0;

bool calibrated = false;
unsigned long lastTime = 0;

// Для комплементарного фильтра
float compPitch = 0, compRoll = 0, compYaw = 0;

// Относительный ноль
float zeroPitch = 0, zeroRoll = 0, zeroYaw = 0;
bool zeroSet = false;

// Накопленные углы (без ограничений) - ИСПРАВЛЕННАЯ ВЕРСИЯ
double accumulatedPitch = 0, accumulatedRoll = 0, accumulatedYaw = 0;

// Предыдущие значения нормализованных углов для вычисления разницы
float prevNormalizedPitch = 0, prevNormalizedRoll = 0, prevNormalizedYaw = 0;
bool firstMeasurement = true;

// Улучшенная компенсация дрейфа
float yawDriftCompensation = 0.0;
float pitchDriftCompensation = 0.0;
float rollDriftCompensation = 0.0;

// Фильтр низких частот для гироскопа
const float GYRO_LPF_ALPHA = 0.9;
float filteredGx = 0, filteredGy = 0, filteredGz = 0;

// Настройки отправки данных
unsigned long lastDataSend = 0;
const unsigned long SEND_INTERVAL = 50;
const float CHANGE_THRESHOLD = 0.5;

// Глобальные массивы для калибровки (только для гироскопа, чтобы экономить память)
float gxSamples[CALIBRATION_SAMPLES];
float gySamples[CALIBRATION_SAMPLES];
float gzSamples[CALIBRATION_SAMPLES];

// Установка относительного нуля
void setZeroPoint() {
  zeroPitch = accumulatedPitch;
  zeroRoll = accumulatedRoll;
  zeroYaw = accumulatedYaw;
  zeroSet = true;
  
  Serial.println("Zero point set");
  Serial.print("Zero Pitch: "); Serial.print(zeroPitch);
  Serial.print(" Roll: "); Serial.print(zeroRoll);
  Serial.print(" Yaw: "); Serial.println(zeroYaw);
  
  String message = "ZERO_SET:PITCH:" + String(zeroPitch, 2) + 
                   ",ROLL:" + String(zeroRoll, 2) + 
                   ",YAW:" + String(zeroYaw, 2);
  sendBluetoothMessage(message);
}

// Сброс относительного нуля
void resetZeroPoint() {
  zeroPitch = 0;
  zeroRoll = 0;
  zeroYaw = 0;
  zeroSet = false;
  
  Serial.println("Zero point reset");
  sendBluetoothMessage("ZERO_RESET");
}

// Нормализация угла для отображения в диапазоне -180..180
float normalizeAngle(float angle) {
  float normalized = fmod(angle, 360.0);
  if (normalized > 180) normalized -= 360;
  if (normalized < -180) normalized += 360;
  return normalized;
}

// Функция для правильного вычисления разницы углов с учетом переходов через ±180
float angleDifference(float current, float previous) {
  float diff = current - previous;
  
  // Корректируем разницу для переходов через границу
  if (diff > 180) {
    diff -= 360;
  } else if (diff < -180) {
    diff += 360;
  }
  
  return diff;
}

// Обновление накопленных углов (используем разницу между нормализованными углами)
void updateAccumulatedAngles() {
  if (firstMeasurement) {
    prevNormalizedPitch = displayPitch;
    prevNormalizedRoll = displayRoll;
    prevNormalizedYaw = displayYaw;
    firstMeasurement = false;
    return;
  }
  
  // Вычисляем разницу с учетом переходов через границы
  float deltaPitch = angleDifference(displayPitch, prevNormalizedPitch);
  float deltaRoll = angleDifference(displayRoll, prevNormalizedRoll);
  float deltaYaw = angleDifference(displayYaw, prevNormalizedYaw);
  
  // Добавляем разницу к накопленным углам
  accumulatedPitch += deltaPitch;
  accumulatedRoll += deltaRoll;
  accumulatedYaw += deltaYaw;
  
  // Обновляем предыдущие значения
  prevNormalizedPitch = displayPitch;
  prevNormalizedRoll = displayRoll;
  prevNormalizedYaw = displayYaw;
}

// Получение относительных углов (разница от нулевой точки)
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

// Фильтр низких частот
float lowPassFilter(float current, float previous, float alpha) {
  return alpha * previous + (1.0 - alpha) * current;
}

// Калибровка сенсора - упрощенная версия без массивов для акселерометра
void calibrateSensor() {
  Serial.println("Calibrating MPU6050...");
  
  float sumGx = 0, sumGy = 0, sumGz = 0;
  float sumAx = 0, sumAy = 0, sumAz = 0;
  
  // Прогрев
  for (int i = 0; i < 50; i++) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x43);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, 6, true);
    while (Wire.available() < 6);
    Wire.read(); Wire.read(); Wire.read(); Wire.read(); Wire.read(); Wire.read();
    delay(10);
  }
  
  // Калибровка гироскопа и акселерометра
  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, 14, true);
    
    if (Wire.available() >= 14) {
      int16_t ax_raw = Wire.read() << 8 | Wire.read();
      int16_t ay_raw = Wire.read() << 8 | Wire.read();
      int16_t az_raw = Wire.read() << 8 | Wire.read();
      
      Wire.read(); Wire.read(); // Температура
      
      int16_t gx_raw = Wire.read() << 8 | Wire.read();
      int16_t gy_raw = Wire.read() << 8 | Wire.read();
      int16_t gz_raw = Wire.read() << 8 | Wire.read();
      
      // Гироскоп
      float gx = gx_raw / 131.0;
      float gy = gy_raw / 131.0;
      float gz = gz_raw / 131.0;
      
      // Сохраняем только гироскоп в массивы для вычисления стандартного отклонения
      gxSamples[i] = gx;
      gySamples[i] = gy;
      gzSamples[i] = gz;
      
      sumGx += gx;
      sumGy += gy;
      sumGz += gz;
      
      // Акселерометр - только суммируем, не сохраняем в массивы
      float ax = ax_raw / 16384.0;
      float ay = ay_raw / 16384.0;
      float az = az_raw / 16384.0;
      
      sumAx += ax;
      sumAy += ay;
      sumAz += az;
    }
    delay(CALIBRATION_DELAY);
    
    if (i % 20 == 0) Serial.print(".");
  }
  Serial.println();
  
  // Вычисляем смещения
  gyroOffsetX = sumGx / CALIBRATION_SAMPLES;
  gyroOffsetY = sumGy / CALIBRATION_SAMPLES;
  gyroOffsetZ = sumGz / CALIBRATION_SAMPLES;
  
  accelOffsetX = sumAx / CALIBRATION_SAMPLES;
  accelOffsetY = sumAy / CALIBRATION_SAMPLES;
  accelOffsetZ = (sumAz / CALIBRATION_SAMPLES) - 1.0; // Вычитаем гравитацию (1g)
  
  // Вычисляем стандартное отклонение только для гироскопа
  float stdGx = 0, stdGy = 0, stdGz = 0;
  
  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    stdGx += pow(gxSamples[i] - gyroOffsetX, 2);
    stdGy += pow(gySamples[i] - gyroOffsetY, 2);
    stdGz += pow(gzSamples[i] - gyroOffsetZ, 2);
  }
  
  stdGx = sqrt(stdGx / CALIBRATION_SAMPLES);
  stdGy = sqrt(stdGy / CALIBRATION_SAMPLES);
  stdGz = sqrt(stdGz / CALIBRATION_SAMPLES);
  
  // Сбрасываем все состояния
  calibrated = true;
  pitch = 0;
  roll = 0;
  yaw = 0;
  displayPitch = 0;
  displayRoll = 0;
  displayYaw = 0;
  
  // СБРАСЫВАЕМ НАКОПЛЕННЫЕ УГЛЫ
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  prevNormalizedPitch = 0;
  prevNormalizedRoll = 0;
  prevNormalizedYaw = 0;
  firstMeasurement = true;
  
  compPitch = 0;
  compRoll = 0;
  compYaw = 0;
  filteredGx = 0;
  filteredGy = 0;
  filteredGz = 0;
  
  // Сбрасываем компенсацию дрейфа
  pitchDriftCompensation = 0;
  rollDriftCompensation = 0;
  yawDriftCompensation = 0;
  
  Serial.println("\nCalibration complete!");
  Serial.print("Gyro Offsets - X: "); Serial.print(gyroOffsetX, 6);
  Serial.print(" Y: "); Serial.print(gyroOffsetY, 6);
  Serial.print(" Z: "); Serial.println(gyroOffsetZ, 6);
  Serial.print("Accel Offsets - X: "); Serial.print(accelOffsetX, 6);
  Serial.print(" Y: "); Serial.print(accelOffsetY, 6);
  Serial.print(" Z: "); Serial.println(accelOffsetZ, 6);
  Serial.print("Gyro Std Dev - X: "); Serial.print(stdGx, 6);
  Serial.print(" Y: "); Serial.print(stdGy, 6);
  Serial.print(" Z: "); Serial.println(stdGz, 6);
}

// Формирование строки с данными для отправки
String getSensorDataString() {
  // Обновляем накопленные углы
  updateAccumulatedAngles();
  
  float relPitch = getRelativePitch();
  float relRoll = getRelativeRoll();
  float relYaw = getRelativeYaw();
  
  String data = "PITCH:" + String(displayPitch, 1) + 
                ",ROLL:" + String(displayRoll, 1) + 
                ",YAW:" + String(displayYaw, 1) +
                ",REL_PITCH:" + String(relPitch, 2) +
                ",REL_ROLL:" + String(relRoll, 2) +
                ",REL_YAW:" + String(relYaw, 2) +
                ",ACC_PITCH:" + String(accumulatedPitch, 2) +
                ",ACC_ROLL:" + String(accumulatedRoll, 2) +
                ",ACC_YAW:" + String(accumulatedYaw, 2) +
                ",ZERO_SET:" + String(zeroSet ? "true" : "false") +
                ",UNLIMITED:true";
  
  return data;
}

// Проверка изменения данных для отправки
bool dataChanged() {
  return (abs(displayPitch - lastSentPitch) >= CHANGE_THRESHOLD ||
          abs(displayRoll - lastSentRoll) >= CHANGE_THRESHOLD ||
          abs(displayYaw - lastSentYaw) >= CHANGE_THRESHOLD);
}

// Основная функция расчета углов
void calculateAngles() {
  if (!calibrated) return;
  
  static unsigned long lastCalcTime = 0;
  unsigned long currentTime = micros();
  float deltaTime = (currentTime - lastCalcTime) / 1000000.0;
  
  if (lastCalcTime == 0 || deltaTime > 0.1) {
    deltaTime = 0.01;
  }
  lastCalcTime = currentTime;
  
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 14, true);
  
  if (Wire.available() >= 14) {
    int16_t ax_raw = Wire.read() << 8 | Wire.read();
    int16_t ay_raw = Wire.read() << 8 | Wire.read();
    int16_t az_raw = Wire.read() << 8 | Wire.read();
    
    int16_t temp_raw = Wire.read() << 8 | Wire.read();
    
    int16_t gx_raw = Wire.read() << 8 | Wire.read();
    int16_t gy_raw = Wire.read() << 8 | Wire.read();
    int16_t gz_raw = Wire.read() << 8 | Wire.read();
    
    // Конвертация с учетом калибровки
    float ax = (ax_raw / 16384.0) - accelOffsetX;
    float ay = (ay_raw / 16384.0) - accelOffsetY;
    float az = (az_raw / 16384.0) - accelOffsetZ;
    
    sensorData.temperature = (temp_raw / 340.0) + 36.53;
    
    // Гироскоп с компенсацией смещения
    float gx = (gx_raw / 131.0) - gyroOffsetX;
    float gy = (gy_raw / 131.0) - gyroOffsetY;
    float gz = (gz_raw / 131.0) - gyroOffsetZ;
    
    // Применяем компенсацию дрейфа
    gx += pitchDriftCompensation;
    gy += rollDriftCompensation;
    gz += yawDriftCompensation;
    
    // Применяем фильтр низких частот для уменьшения шума
    filteredGx = lowPassFilter(gx, filteredGx, GYRO_LPF_ALPHA);
    filteredGy = lowPassFilter(gy, filteredGy, GYRO_LPF_ALPHA);
    filteredGz = lowPassFilter(gz, filteredGz, GYRO_LPF_ALPHA);
    
    // Используем отфильтрованные значения
    gx = filteredGx;
    gy = filteredGy;
    gz = filteredGz;
    
    // Сохраняем сырые данные
    sensorData.ax = ax;
    sensorData.ay = ay;
    sensorData.az = az;
    sensorData.gx = gx;
    sensorData.gy = gy;
    sensorData.gz = gz;
    sensorData.uptime = millis() / 1000;
    
    // ДЕЛЬТЫ углов
    float deltaPitch = gx * deltaTime;
    float deltaRoll = gy * deltaTime;
    float deltaYaw = gz * deltaTime;
    
    // Расчет углов по акселерометру
    float accelPitch = 0, accelRoll = 0;
    float accelMagnitude = sqrt(ax*ax + ay*ay + az*az);
    
    if (accelMagnitude > 0.8 && accelMagnitude < 1.2) {
      accelPitch = atan2(ay, sqrt(ax*ax + az*az)) * 180.0 / PI;
      accelRoll = atan2(-ax, sqrt(ay*ay + az*az)) * 180.0 / PI;
    }
    
    // Комплементарный фильтр
    static float lastAccelPitch = 0, lastAccelRoll = 0;
    
    if (accelMagnitude > 0.8 && accelMagnitude < 1.2) {
      if (abs(accelPitch - lastAccelPitch) < 30) {
        compPitch = 0.98 * (compPitch + deltaPitch) + 0.02 * accelPitch;
      } else {
        compPitch += deltaPitch;
      }
      
      if (abs(accelRoll - lastAccelRoll) < 30) {
        compRoll = 0.98 * (compRoll + deltaRoll) + 0.02 * accelRoll;
      } else {
        compRoll += deltaRoll;
      }
      
      lastAccelPitch = accelPitch;
      lastAccelRoll = accelRoll;
    } else {
      compPitch += deltaPitch;
      compRoll += deltaRoll;
    }
    
    // Для Yaw используем только гироскоп
    compYaw += deltaYaw;
    
    // Текущие углы
    pitch = compPitch;
    roll = compRoll;
    yaw = compYaw;
    
    // Углы для отображения (нормализованные -180..180)
    displayPitch = normalizeAngle(pitch);
    displayRoll = normalizeAngle(roll);
    displayYaw = normalizeAngle(yaw);
    
    // Отладочный вывод каждые 10 секунд
    static unsigned long lastDebugPrint = 0;
    if (millis() - lastDebugPrint > 10000) {
      Serial.print("Display: P="); Serial.print(displayPitch, 1);
      Serial.print("°, R="); Serial.print(displayRoll, 1);
      Serial.print("°, Y="); Serial.print(displayYaw, 1); Serial.println("°");
      
      Serial.print("Accumulated: P="); Serial.print(accumulatedPitch, 1);
      Serial.print("°, R="); Serial.print(accumulatedRoll, 1);
      Serial.print("°, Y="); Serial.print(accumulatedYaw, 1); Serial.println("°");
      
      Serial.print("Gyro: X="); Serial.print(gx, 3);
      Serial.print("°/s, Y="); Serial.print(gy, 3);
      Serial.print("°/s, Z="); Serial.print(gz, 3); Serial.println("°/s");
      
      lastDebugPrint = millis();
    }
  }
}

// Отправка сообщения через Bluetooth
void sendBluetoothMessage(String message) {
  if (deviceConnected && pCharacteristic != NULL) {
    pCharacteristic->setValue(message.c_str());
    pCharacteristic->notify();
  }
}

// Класс обратного вызова для BLE сервера
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Устройство подключено по Bluetooth");
      digitalWrite(LED_PIN, HIGH);
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Устройство отключено от Bluetooth");
      digitalWrite(LED_PIN, LOW);
    }
};

// Класс обратного вызова для BLE характеристики
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      String value = pCharacteristic->getValue();
      
      if (value.length() > 0) {
        Serial.print("Получено по Bluetooth: ");
        Serial.println(value);
        
        if (value == "GET_DATA") {
          String data = getSensorDataString();
          sendBluetoothMessage(data);
        }
        else if (value == "RECALIBRATE") {
          calibrated = false;
          calibrateSensor();
          sendBluetoothMessage("RECALIBRATION_COMPLETE");
        }
        else if (value == "RESET_ANGLES") {
          accumulatedPitch = 0;
          accumulatedRoll = 0;
          accumulatedYaw = 0;
          pitch = 0;
          roll = 0;
          yaw = 0;
          displayPitch = 0;
          displayRoll = 0;
          displayYaw = 0;
          compPitch = 0;
          compRoll = 0;
          compYaw = 0;
          lastSentPitch = 0;
          lastSentRoll = 0;
          lastSentYaw = 0;
          
          // Сбрасываем состояния накопления
          prevNormalizedPitch = 0;
          prevNormalizedRoll = 0;
          prevNormalizedYaw = 0;
          firstMeasurement = true;
          
          if (zeroSet) {
            zeroPitch = 0;
            zeroRoll = 0;
            zeroYaw = 0;
          }
          
          sendBluetoothMessage("ANGLES_RESET");
          String data = getSensorDataString();
          sendBluetoothMessage(data);
        }
        else if (value == "SET_ZERO") {
          setZeroPoint();
          sendBluetoothMessage("ZERO_POINT_SET");
        }
        else if (value == "RESET_ZERO") {
          resetZeroPoint();
          sendBluetoothMessage("ZERO_POINT_RESET");
        }
        else if (value == "ADJUST_DRIFT") {
          // Ручная корректировка дрейфа на основе текущих показаний
          yawDriftCompensation -= sensorData.gz * 0.5;
          pitchDriftCompensation -= sensorData.gx * 0.5;
          rollDriftCompensation -= sensorData.gy * 0.5;
          
          String msg = "DRIFT_ADJUSTED:P=" + String(pitchDriftCompensation, 6) +
                       ",R=" + String(rollDriftCompensation, 6) +
                       ",Y=" + String(yawDriftCompensation, 6);
          sendBluetoothMessage(msg);
        }
        else if (value == "RESET_DRIFT_COMP") {
          pitchDriftCompensation = 0;
          rollDriftCompensation = 0;
          yawDriftCompensation = 0;
          sendBluetoothMessage("DRIFT_COMPENSATION_RESET");
        }
        else if (value == "LED ON") {
          digitalWrite(LED_PIN, HIGH);
          sendBluetoothMessage("LED_ON");
        }
        else if (value == "LED OFF") {
          digitalWrite(LED_PIN, LOW);
          sendBluetoothMessage("LED_OFF");
        }
        else if (value == "STATUS") {
          String status = "STATUS:StableUnlimited,Uptime:" + String(millis() / 1000) + 
                         "s,Calibrated:" + (calibrated ? "Yes" : "No") +
                         ",ZeroSet:" + (zeroSet ? "Yes" : "No") +
                         ",Temp:" + String(sensorData.temperature, 1) + "C" +
                         ",DriftP:" + String(pitchDriftCompensation, 6) +
                         ",DriftR:" + String(rollDriftCompensation, 6) +
                         ",DriftY:" + String(yawDriftCompensation, 6);
          sendBluetoothMessage(status);
        }
        else if (value == "TEMP") {
          String tempMsg = "TEMPERATURE:" + String(sensorData.temperature, 2) + "C";
          sendBluetoothMessage(tempMsg);
        }
        else if (value == "DATA") {
          String rawData = "RAW:AX:" + String(sensorData.ax, 3) + 
                           ",AY:" + String(sensorData.ay, 3) + 
                           ",AZ:" + String(sensorData.az, 3) + 
                           ",GX:" + String(sensorData.gx, 3) + 
                           ",GY:" + String(sensorData.gy, 3) + 
                           ",GZ:" + String(sensorData.gz, 3) + 
                           ",TEMP:" + String(sensorData.temperature, 2);
          sendBluetoothMessage(rawData);
        }
        else if (value == "ACCEL") {
          String accelData = "ACCEL:AX:" + String(sensorData.ax, 3) + 
                             ",AY:" + String(sensorData.ay, 3) + 
                             ",AZ:" + String(sensorData.az, 3);
          sendBluetoothMessage(accelData);
        }
        else if (value == "GYRO") {
          String gyroData = "GYRO:GX:" + String(sensorData.gx, 3) + 
                            ",GY:" + String(sensorData.gy, 3) + 
                            ",GZ:" + String(sensorData.gz, 3);
          sendBluetoothMessage(gyroData);
        }
        else if (value == "SET_ANGLE:PITCH") {
          accumulatedPitch = 0;
          sendBluetoothMessage("PITCH_RESET_TO_ZERO");
        }
        else if (value == "SET_ANGLE:ROLL") {
          accumulatedRoll = 0;
          sendBluetoothMessage("ROLL_RESET_TO_ZERO");
        }
        else if (value == "SET_ANGLE:YAW") {
          accumulatedYaw = 0;
          sendBluetoothMessage("YAW_RESET_TO_ZERO");
        }
        else if (value == "RESTART") {
          sendBluetoothMessage("RESTARTING...");
          delay(100);
          ESP.restart();
        } 
        else {
          sendBluetoothMessage("ECHO:" + value);
        }
      }
    }
};

// Инициализация MPU6050
void initMPU6050() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission(true);
  delay(100);
  
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B);
  Wire.write(0x00);
  Wire.endTransmission(true);
  delay(10);
  
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x00);
  Wire.endTransmission(true);
  delay(10);
  
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1A);
  Wire.write(0x03);
  Wire.endTransmission(true);
  delay(10);
  
  Serial.println("MPU6050 initialized");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== VR Head Tracker - Stable Unlimited Angles V2 ===");
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  delay(100);
  
  Wire.beginTransmission(MPU_ADDR);
  byte error = Wire.endTransmission();
  if (error == 0) {
    Serial.println("MPU6050 found at address 0x68");
  } else {
    Serial.println("MPU6050 not found!");
    while(1) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  initMPU6050();
  
  Serial.println("Starting calibration... (keep device stationary!)");
  calibrateSensor();
  
  Serial.println("Initializing BLE server...");
  BLEDevice::init("VR_Head_Stable_Fixed");
  
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->setValue("VR Head Tracker - Stable Unlimited Angles");
  
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  
  BLEDevice::startAdvertising();
  
  Serial.println("\n=== BLE Server Ready ===");
  Serial.println("Device Name: VR_Head_Stable_Fixed");
  Serial.println("\n=== UNLIMITED ANGLES FIXED ===");
  Serial.println("Features:");
  Serial.println("1. Unlimited accumulated angles (can exceed 360°)");
  Serial.println("2. Display angles normalized to -180..180");
  Serial.println("3. Proper handling of angle transitions through ±180");
  Serial.println("4. Relative angles from zero point");
  Serial.println("5. Drift compensation");
  Serial.println("6. All BLE commands preserved");
  
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(300);
    digitalWrite(LED_PIN, LOW);
    delay(300);
  }
  
  lastDataSend = millis();
}

void loop() {
  calculateAngles();
  
  if (deviceConnected) {
    unsigned long currentTime = millis();
    
    if (currentTime - lastDataSend >= SEND_INTERVAL) {
      if (dataChanged() || lastDataSend == 0) {
        String data = getSensorDataString();
        pCharacteristic->setValue(data.c_str());
        pCharacteristic->notify();
        
        lastSentPitch = displayPitch;
        lastSentRoll = displayRoll;
        lastSentYaw = displayYaw;
        lastDataSend = currentTime;
      }
    }
  }
  
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Started advertising BLE...");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  delay(5);
}
