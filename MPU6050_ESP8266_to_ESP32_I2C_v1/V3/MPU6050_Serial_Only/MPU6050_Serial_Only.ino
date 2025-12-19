/*
  MPU6050 VR Head Tracker - Serial Only Version
  Улучшенная калибровка и ориентация для VR шлема
  Только вывод через Serial порт в формате JSON
*/

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <EEPROM.h>

// Структура для хранения калибровочных данных в EEPROM
struct CalibrationData {
  uint8_t validMarker[4] = {'M', 'P', 'U', '6'};
  float gyroX_offset = 0;
  float gyroY_offset = 0;
  float gyroZ_offset = 0;
  float accelX_offset = 0;
  float accelY_offset = 0;
  float accelZ_offset = 0;
};

Adafruit_MPU6050 mpu;
CalibrationData calibData;

// Текущие углы ориентации (в градусах)
float pitch = 0;   // Наклон вперед/назад (вращение вокруг оси X)
float roll = 0;    // Наклон влево/вправо (вращение вокруг оси Y)
float yaw = 0;     // Поворот головы (вращение вокруг оси Z)

// Дополнительные углы для VR
float smoothedPitch = 0;
float smoothedRoll = 0;
float smoothedYaw = 0;
const float smoothingFactor = 0.3;

// Нулевая точка (референсная позиция)
float zeroPitch = 0;
float zeroRoll = 0;
float zeroYaw = 0;
bool zeroSet = false;

// Контроль направления вращения
int pitchDirection = 0;
int rollDirection = 0;
int yawDirection = 0;

// Предыдущие значения для детектирования движения
float prevPitch = 0, prevRoll = 0, prevYaw = 0;
const float MOVEMENT_THRESHOLD = 0.5;

// Время для интеграции
unsigned long lastTime = 0;

// Адрес I2C
uint8_t current_i2c_address = 0x68;

// Параметры вывода в Serial
unsigned long lastSerialOutput = 0;
const unsigned long SERIAL_OUTPUT_INTERVAL = 100; // 100ms = 10Hz

// Прототипы функций
bool scanI2CForMPU();
bool loadCalibrationData();
void saveCalibrationData();
void calibrateGyroAccel();
void setZeroPoint();
void resetZeroPoint();
void printHelp();
void updateAngles(sensors_event_t &a, sensors_event_t &g, float dt);
void applyVRCorrections();
void updateMovementDirection();
void outputVRData();
void handleSerialCommands();

void setup() {
  // Инициализация Serial порта
  Serial.begin(115200);
  delay(100);
  
  Serial.println();
  Serial.println("===================================================");
  Serial.println("🎮 MPU6050 VR Head Tracker - Serial Only Version");
  Serial.println("===================================================");
  Serial.println("📡 Выходные данные в формате JSON:");
  Serial.println("   angles - относительные углы в градусах");
  Serial.println("   absolute - абсолютные углы в градусах");
  Serial.println("   zero_set - флаг установки нулевой точки");
  Serial.println("   direction - направление движения");
  Serial.println("===================================================");
  Serial.println("📋 Доступные команды:");
  Serial.println("   help    - показать справку");
  Serial.println("   calib   - выполнить калибровку");
  Serial.println("   zero    - установить нулевую точку");
  Serial.println("   reset   - сбросить нулевую точку");
  Serial.println("   status  - показать статус");
  Serial.println("   save    - сохранить калибровку в EEPROM");
  Serial.println("   load    - загрузить калибровку из EEPROM");
  Serial.println("===================================================");
  
  // Инициализация EEPROM
  EEPROM.begin(sizeof(CalibrationData));
  
  // Инициализация I2C
  Wire.begin();
  
  // Поиск MPU6050 на шине I2C
  Serial.println("🔍 Поиск MPU6050 на I2C шине...");
  
  if (!scanI2CForMPU()) {
    Serial.println("❌ MPU6050 не найден!");
    Serial.println("   Пожалуйста, проверьте подключение датчика:");
    Serial.println("   - SDA -> GPIO4 (D2)");
    Serial.println("   - SCL -> GPIO5 (D1)");
    Serial.println("   - VCC -> 3.3V");
    Serial.println("   - GND -> GND");
    while (1) {
      delay(1000);
    }
  }
  
  // Загрузка калибровочных данных
  Serial.println("\n📂 Загрузка калибровочных данных...");
  if (loadCalibrationData()) {
    Serial.println("✅ Калибровочные данные загружены из EEPROM");
  } else {
    Serial.println("⚠️ Калибровочные данные не найдены");
    Serial.println("🔧 Выполнение калибровки...");
    calibrateGyroAccel();
  }
  
  // Настройка датчика для VR
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
  
  Serial.println("\n✅ Система готова!");
  Serial.println("🎯 Инициализация времени...");
  
  // Инициализация времени
  lastTime = micros();
  lastSerialOutput = millis();
  
  Serial.println("===================================================");
  Serial.println("🚀 Начинаю отслеживание...");
  Serial.println();
}

void loop() {
  // Обработка входящих команд
  if (Serial.available() > 0) {
    handleSerialCommands();
  }
  
  // Получаем данные с датчика
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    Serial.println("❌ Ошибка чтения данных с MPU6050");
    delay(100);
    return;
  }
  
  // Рассчитываем время с момента последнего измерения
  unsigned long current_time = micros();
  float dt = (current_time - lastTime) / 1000000.0;
  lastTime = current_time;
  
  if (dt <= 0) dt = 0.0001;
  
  // Обновляем углы
  updateAngles(a, g, dt);
  
  // Применяем коррекции для VR
  applyVRCorrections();
  
  // Определяем направление движения
  updateMovementDirection();
  
  // Выводим данные в Serial
  unsigned long currentMillis = millis();
  if (currentMillis - lastSerialOutput >= SERIAL_OUTPUT_INTERVAL) {
    outputVRData();
    lastSerialOutput = currentMillis;
  }
  
  // Небольшая задержка для стабильности
  delay(10);
}

// Обновление углов ориентации
void updateAngles(sensors_event_t &a, sensors_event_t &g, float dt) {
  // Компенсируем смещения
  float gyroX = g.gyro.x - calibData.gyroX_offset;
  float gyroY = g.gyro.y - calibData.gyroY_offset;
  float gyroZ = g.gyro.z - calibData.gyroZ_offset;
  
  float ax = a.acceleration.x - calibData.accelX_offset;
  float ay = a.acceleration.y - calibData.accelY_offset;
  float az = a.acceleration.z - calibData.accelZ_offset;
  
  // Преобразуем в градусы/секунду
  float gyroX_rate = gyroX * (180.0 / PI);   // Pitch (вращение вокруг X)
  float gyroY_rate = gyroY * (180.0 / PI);   // Roll (вращение вокруг Y)
  float gyroZ_rate = gyroZ * (180.0 / PI);   // Yaw (вращение вокруг Z)
  
  // Интегрируем угловые скорости
  float rawPitch = pitch + gyroX_rate * dt;
  float rawRoll = roll + gyroY_rate * dt;
  float rawYaw = yaw + gyroZ_rate * dt;
  
  // Рассчитываем углы от акселерометра (для стабилизации)
  float accel_pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180 / PI;
  float accel_roll = atan2(ay, sqrt(ax * ax + az * az)) * 180 / PI;
  
  // Комплементарный фильтр
  float alpha = 0.96;
  pitch = alpha * rawPitch + (1 - alpha) * accel_pitch;
  roll = alpha * rawRoll + (1 - alpha) * accel_roll;
  yaw = rawYaw;  // Для yaw используем только гироскоп
  
  // Нормализация углов
  if (pitch > 180) pitch -= 360;
  if (pitch < -180) pitch += 360;
  if (roll > 180) roll -= 360;
  if (roll < -180) roll += 360;
  if (yaw > 180) yaw -= 360;
  if (yaw < -180) yaw += 360;
}

// Применение коррекций для VR
void applyVRCorrections() {
  // Сглаживание для плавного движения
  smoothedPitch = smoothedPitch * (1 - smoothingFactor) + pitch * smoothingFactor;
  smoothedRoll = smoothedRoll * (1 - smoothingFactor) + roll * smoothingFactor;
  smoothedYaw = smoothedYaw * (1 - smoothingFactor) + yaw * smoothingFactor;
  
  // Автоматическая коррекция дрейфа при малой скорости вращения
  static float yawDriftAccumulator = 0;
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  float gyroZ = g.gyro.z - calibData.gyroZ_offset;
  if (abs(gyroZ) < 0.002) { // Очень медленное вращение
    yawDriftAccumulator += 0.0001;
    if (abs(yawDriftAccumulator) > 0.01) {
      yaw -= yawDriftAccumulator;
      smoothedYaw -= yawDriftAccumulator;
      yawDriftAccumulator = 0;
    }
  } else {
    yawDriftAccumulator = 0;
  }
}

// Обновление направления движения
void updateMovementDirection() {
  pitchDirection = (abs(smoothedPitch - prevPitch) < MOVEMENT_THRESHOLD) ? 0 : 
                   ((smoothedPitch > prevPitch) ? 1 : -1);
  rollDirection = (abs(smoothedRoll - prevRoll) < MOVEMENT_THRESHOLD) ? 0 : 
                  ((smoothedRoll > prevRoll) ? 1 : -1);
  yawDirection = (abs(smoothedYaw - prevYaw) < MOVEMENT_THRESHOLD) ? 0 : 
                 ((smoothedYaw > prevYaw) ? 1 : -1);
  
  prevPitch = smoothedPitch;
  prevRoll = smoothedRoll;
  prevYaw = smoothedYaw;
}

// Вывод данных для VR в формате JSON
void outputVRData() {
  float relPitch = zeroSet ? smoothedPitch - zeroPitch : smoothedPitch;
  float relRoll = zeroSet ? smoothedRoll - zeroRoll : smoothedRoll;
  float relYaw = zeroSet ? smoothedYaw - zeroYaw : smoothedYaw;
  
  // Создаем JSON объект
  Serial.print("{");
  
  // Относительные углы
  Serial.print("\"angles\":{");
  Serial.print("\"pitch\":");
  Serial.print(relPitch, 1);
  Serial.print(",\"roll\":");
  Serial.print(relRoll, 1);
  Serial.print(",\"yaw\":");
  Serial.print(relYaw, 1);
  Serial.print("},");
  
  // Абсолютные углы
  Serial.print("\"absolute\":{");
  Serial.print("\"pitch\":");
  Serial.print(smoothedPitch, 1);
  Serial.print(",\"roll\":");
  Serial.print(smoothedRoll, 1);
  Serial.print(",\"yaw\":");
  Serial.print(smoothedYaw, 1);
  Serial.print("},");
  
  // Флаг нулевой точки
  Serial.print("\"zero_set\":");
  Serial.print(zeroSet ? "true" : "false");
  Serial.print(",");
  
  // Направление движения
  Serial.print("\"direction\":{");
  Serial.print("\"pitch\":");
  Serial.print(pitchDirection);
  Serial.print(",\"roll\":");
  Serial.print(rollDirection);
  Serial.print(",\"yaw\":");
  Serial.print(yawDirection);
  Serial.print("}");
  
  Serial.println("}");
}

// Поиск MPU6050 на I2C шине
bool scanI2CForMPU() {
  Serial.println("🔍 Сканирую I2C адреса...");
  
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("   Найдено устройство на 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      
      if (mpu.begin(address)) {
        Serial.println(" - MPU6050 обнаружен!");
        current_i2c_address = address;
        delay(100);
        return true;
      } else {
        Serial.println(" - не MPU6050");
      }
    }
  }
  
  return false;
}

// Загрузка калибровочных данных
bool loadCalibrationData() {
  EEPROM.get(0, calibData);
  
  if (calibData.validMarker[0] == 'M' &&
      calibData.validMarker[1] == 'P' &&
      calibData.validMarker[2] == 'U' &&
      calibData.validMarker[3] == '6') {
    return true;
  }
  return false;
}

// Сохранение калибровочных данных
void saveCalibrationData() {
  calibData.validMarker[0] = 'M';
  calibData.validMarker[1] = 'P';
  calibData.validMarker[2] = 'U';
  calibData.validMarker[3] = '6';
  
  EEPROM.put(0, calibData);
  EEPROM.commit();
  
  Serial.println("✅ Калибровочные данные сохранены в EEPROM");
}

// Полная калибровка
void calibrateGyroAccel() {
  Serial.println("🎯 Начинаю калибровку...");
  Serial.println("📏 Оставьте устройство неподвижно на ровной поверхности");
  
  for (int i = 3; i > 0; i--) {
    Serial.print("   ");
    Serial.println(i);
    delay(1000);
  }
  
  Serial.println("🔧 Калибрую... Не двигайте устройство!");
  
  float sumGyroX = 0, sumGyroY = 0, sumGyroZ = 0;
  float sumAccelX = 0, sumAccelY = 0, sumAccelZ = 0;
  int samples = 2000;
  
  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    
    sumGyroX += g.gyro.x;
    sumGyroY += g.gyro.y;
    sumGyroZ += g.gyro.z;
    
    sumAccelX += a.acceleration.x;
    sumAccelY += a.acceleration.y;
    sumAccelZ += a.acceleration.z;
    
    if (i % 200 == 0) {
      Serial.print(".");
    }
    delay(1);
  }
  
  // Расчет смещений
  calibData.gyroX_offset = sumGyroX / samples;
  calibData.gyroY_offset = sumGyroY / samples;
  calibData.gyroZ_offset = sumGyroZ / samples;
  
  calibData.accelX_offset = sumAccelX / samples;
  calibData.accelY_offset = sumAccelY / samples;
  calibData.accelZ_offset = (sumAccelZ / samples) - 9.81;
  
  Serial.println("\n✅ Калибровка завершена!");
  
  // Сохраняем данные
  saveCalibrationData();
  
  Serial.println("📊 Полученные смещения:");
  Serial.print("   Гироскоп - X:");
  Serial.print(calibData.gyroX_offset, 6);
  Serial.print(" Y:");
  Serial.print(calibData.gyroY_offset, 6);
  Serial.print(" Z:");
  Serial.println(calibData.gyroZ_offset, 6);
  
  Serial.print("   Акселерометр - X:");
  Serial.print(calibData.accelX_offset, 6);
  Serial.print(" Y:");
  Serial.print(calibData.accelY_offset, 6);
  Serial.print(" Z:");
  Serial.println(calibData.accelZ_offset, 6);
}

// Установка нулевой точки
void setZeroPoint() {
  zeroPitch = smoothedPitch;
  zeroRoll = smoothedRoll;
  zeroYaw = smoothedYaw;
  zeroSet = true;
  
  Serial.println("🎯 Нулевая точка установлена!");
  Serial.print("   Pitch: ");
  Serial.print(zeroPitch, 1);
  Serial.print("°, Roll: ");
  Serial.print(zeroRoll, 1);
  Serial.print("°, Yaw: ");
  Serial.print(zeroYaw, 1);
  Serial.println("°");
}

// Сброс нулевой точки
void resetZeroPoint() {
  zeroPitch = 0;
  zeroRoll = 0;
  zeroYaw = 0;
  zeroSet = false;
  
  Serial.println("🔄 Нулевая точка сброшена");
}

// Вывод справки
void printHelp() {
  Serial.println("\n📋 Доступные команды:");
  Serial.println("   help    - показать эту справку");
  Serial.println("   calib   - выполнить полную калибровку");
  Serial.println("   zero    - установить текущую позицию как нулевую");
  Serial.println("   reset   - сбросить нулевую точку");
  Serial.println("   status  - показать текущий статус");
  Serial.println("   save    - сохранить калибровку в EEPROM");
  Serial.println("   load    - загрузить калибровку из EEPROM");
  Serial.println();
  Serial.println("📊 Формат вывода данных (JSON):");
  Serial.println("   {");
  Serial.println("     \"angles\": {\"pitch\":0.0,\"roll\":0.0,\"yaw\":0.0},");
  Serial.println("     \"absolute\": {\"pitch\":0.0,\"roll\":0.0,\"yaw\":0.0},");
  Serial.println("     \"zero_set\": true,");
  Serial.println("     \"direction\": {\"pitch\":0,\"roll\":0,\"yaw\":0}");
  Serial.println("   }");
  Serial.println("   angles - относительные углы в градусах");
  Serial.println("   absolute - абсолютные углы в градусах");
  Serial.println("   zero_set - флаг установки нулевой точки (true/false)");
  Serial.println("   direction - направление движения (1=вправо/вверх, -1=влево/вниз, 0=нет движения)");
  Serial.println();
}

// Обработка команд из Serial
void handleSerialCommands() {
  String command = Serial.readStringUntil('\n');
  command.trim();
  
  if (command == "help") {
    printHelp();
  } else if (command == "calib") {
    calibrateGyroAccel();
  } else if (command == "zero") {
    setZeroPoint();
  } else if (command == "reset") {
    resetZeroPoint();
  } else if (command == "status") {
    Serial.println("\n📊 Текущий статус:");
    Serial.print("   Адрес I2C: 0x");
    if (current_i2c_address < 16) Serial.print("0");
    Serial.println(current_i2c_address, HEX);
    Serial.print("   Нулевая точка: ");
    Serial.println(zeroSet ? "Установлена" : "Не установлена");
    if (zeroSet) {
      Serial.print("      Pitch: ");
      Serial.print(zeroPitch, 1);
      Serial.print("°, Roll: ");
      Serial.print(zeroRoll, 1);
      Serial.print("°, Yaw: ");
      Serial.print(zeroYaw, 1);
      Serial.println("°");
    }
    Serial.print("   Текущие углы: Pitch=");
    Serial.print(smoothedPitch, 1);
    Serial.print("°, Roll=");
    Serial.print(smoothedRoll, 1);
    Serial.print("°, Yaw=");
    Serial.print(smoothedYaw, 1);
    Serial.println("°");
  } else if (command == "save") {
    saveCalibrationData();
  } else if (command == "load") {
    if (loadCalibrationData()) {
      Serial.println("✅ Калибровочные данные загружены");
    } else {
      Serial.println("❌ Калибровочные данные не найдены");
    }
  } else if (command.length() > 0) {
    Serial.print("❌ Неизвестная команда: ");
    Serial.println(command);
    Serial.println("Введите 'help' для списка команд");
  }
}
