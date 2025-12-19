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
  float zero_pitch = 0;
  float zero_roll = 0;
  float zero_yaw = 0;
  uint8_t i2c_address = 0x68;
};

Adafruit_MPU6050 mpu;
CalibrationData calibData;

// Variables for data smoothing and orientation tracking
float smoothedPitch = 0;
float smoothedRoll = 0;
float smoothedYaw = 0;
const float smoothingFactor = 0.3;

// Complementary filter variables
float pitch = 0, roll = 0, yaw = 0;
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
bool calibrated = false;
unsigned long lastTime = 0;
unsigned long calibrationStart = 0;
const unsigned long calibrationTime = 3000;

// Zero point (reference position)
float zeroPitch = 0;
float zeroRoll = 0;
float zeroYaw = 0;
bool zeroSet = false;

// Углы космического корабля (без ограничений)
float spacecraft_pitch = 0;
float spacecraft_roll = 0;
float spacecraft_yaw = 0;

// Accumulated angles relative to zero point
float accumulatedPitch = 0;
float accumulatedRoll = 0;
float accumulatedYaw = 0;

// Continuous angle tracking variables
float contPitch = 0, contRoll = 0, contYaw = 0;
float prevAbsPitch = 0, prevAbsRoll = 0, prevAbsYaw = 0;
bool firstMeasurement = true;

// Previous continuous angles for direction detection
float prevContPitch = 0, prevContRoll = 0, prevContYaw = 0;

// Rotation direction (1 for positive, -1 for negative, 0 for no movement)
int pitchDirection = 0;
int rollDirection = 0;
int yawDirection = 0;

// Yaw drift compensation
float yawDrift = 0;
const float YAW_DRIFT_COMPENSATION = 0.01;

// Idle yaw increment variables
unsigned long lastIdleYawIncrement = 0;
const unsigned long IDLE_YAW_INCREMENT_INTERVAL = 5000;
bool isDeviceIdle = false;
const float IDLE_THRESHOLD = 0.5;

// Gaze direction calculation
float gazePitch = 0;
float gazeYaw = 0;
float gazeRoll = 0;
const float MAX_GAZE_PITCH = 30.0;
const float MAX_GAZE_YAW = 60.0;

// Head movement smoothing for gaze
float headMovementFiltered = 0;
const float HEAD_MOVEMENT_SMOOTHING = 0.9;

// Data output interval
unsigned long lastSerialOutput = 0;
const unsigned long SERIAL_OUTPUT_INTERVAL = 50; // Send data every 50ms (20Hz)

// Current I2C address
uint8_t current_i2c_address = 0x68;

// Command mode
bool commandMode = false;
String inputString = "";

// Прототипы функций
bool scanI2CForMPU();
bool loadCalibrationData();
void saveCalibrationData();
void performAutoStartSequence();
void printHelp();
void processCommand(String cmd);

void setup() {
  // Initialize serial port
  Serial.begin(115200);
  delay(100); // Wait for serial to initialize
  
  Serial.println();
  Serial.println("========================================");
  Serial.println("MPU6050 Spacecraft Tracker");
  Serial.println("========================================");
  Serial.println("Starting initialization...");
  
  // Инициализация EEPROM
  EEPROM.begin(sizeof(CalibrationData));
  
  // Инициализация I2C
  Wire.begin();
  
  // Поиск MPU6050 на шине I2C
  Serial.println("Scanning I2C bus...");
  
  if (!scanI2CForMPU()) {
    Serial.println("MPU6050 not found on I2C bus!");
    Serial.println("Please check MPU6050 connection!");
    while (1) {
      delay(1000);
    }
  }
  
  // ВЫПОЛНЕНИЕ АВТОМАТИЧЕСКОЙ ПОСЛЕДОВАТЕЛЬНОСТИ ЗАПУСКА
  performAutoStartSequence();
  
  // Инициализация времени
  lastTime = micros();
  lastSerialOutput = millis();
  
  Serial.println("✅ System ready!");
  Serial.println("📤 Data format: JSON via Serial @ 115200 baud");
  Serial.println("Send 'c' for command mode");
  Serial.println("========================================");
}

void loop() {
  // Обработка команд с последовательного порта (только если введена 'c')
  if (commandMode) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') {
        processCommand(inputString);
        inputString = "";
      } else {
        inputString += c;
      }
    }
  } else {
    // Режим обновления ориентации (потоковые данные)
    updateOrientation();
    
    // Проверка на команду переключения режима
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'c' || c == 'C') {
        commandMode = true;
        Serial.println("\nEntering command mode");
        printHelp();
      }
    }
  }
}

// Автоматическая последовательность запуска
void performAutoStartSequence() {
  Serial.println("\n=== AUTOMATIC STARTUP ===");
  
  // Шаг 1: Попытка загрузить сохраненные калибровочные данные
  Serial.println("\n1. Loading calibration data...");
  if (loadCalibrationData()) {
    Serial.println("   ✓ Calibration data loaded from memory");
    
    // Проверка, совпадает ли адрес I2C
    if (calibData.i2c_address != current_i2c_address) {
      Serial.print("   ⚠ I2C address changed: was 0x");
      Serial.print(calibData.i2c_address, HEX);
      Serial.print(", now 0x");
      Serial.println(current_i2c_address, HEX);
      Serial.println("   Recalibration required");
      goto NEED_CALIBRATION;
    }
    
  } else {
    Serial.println("   ✗ No calibration data found");
    goto NEED_CALIBRATION;
  }
  
  // Проверка валидности данных
  Serial.println("\n2. Validating data...");
  if (calibData.validMarker[0] != 'M' || calibData.validMarker[1] != 'P' || 
      calibData.validMarker[2] != 'U' || calibData.validMarker[3] != '6') {
    Serial.println("   ✗ Invalid data marker");
    goto NEED_CALIBRATION;
  }
  
  // Проверка разумности значений
  if (abs(calibData.gyroX_offset) > 0.5 || abs(calibData.gyroY_offset) > 0.5 || 
      abs(calibData.gyroZ_offset) > 0.5) {
    Serial.println("   ✗ Gyro offset values out of reasonable range");
    goto NEED_CALIBRATION;
  }
  
  Serial.println("   ✓ Data valid");
  
  // Загрузка смещений в текущие переменные
  gyroOffsetX = calibData.gyroX_offset;
  gyroOffsetY = calibData.gyroY_offset;
  gyroOffsetZ = calibData.gyroZ_offset;
  
  zeroPitch = calibData.zero_pitch;
  zeroRoll = calibData.zero_roll;
  zeroYaw = calibData.zero_yaw;
  zeroSet = true;
  calibrated = true;
  
  goto START_STREAMING;
  
NEED_CALIBRATION:
  // Шаг 3: Калибровка
  Serial.println("\n3. Performing calibration...");
  calibrateGyroAccel();
  calibrated = true;
  
  // Шаг 4: Установка нулевой ориентации
  Serial.println("\n4. Setting zero orientation...");
  setZeroOrientation();
  zeroSet = true;
  
  // Шаг 5: Сохранение данных
  Serial.println("\n5. Saving calibration data...");
  saveCalibrationData();
  
  // Шаг 6: Повторная загрузка для проверки
  Serial.println("\n6. Verifying saved data...");
  if (loadCalibrationData()) {
    Serial.println("   ✓ Data successfully saved and loaded");
  } else {
    Serial.println("   ✗ Error loading saved data");
  }
  
START_STREAMING:
  // Шаг 7: Запуск потоковых данных
  Serial.println("\n7. Starting orientation data stream...");
  commandMode = false;
  
  Serial.println("\n=== AUTOMATIC STARTUP COMPLETED ===");
}

// Сканирование шины I2C для поиска MPU6050
bool scanI2CForMPU() {
  Serial.println("Starting I2C bus scan...");
  
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("   Found I2C device at 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      
      // Попытка инициализации как MPU6050
      if (mpu.begin(address)) {
        Serial.println(" - detected MPU6050!");
        current_i2c_address = address;
        calibData.i2c_address = address;
        
        // Настройка параметров датчика
        mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
        mpu.setGyroRange(MPU6050_RANGE_250_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
        
        delay(100);
        return true;
      } else {
        Serial.println(" - not MPU6050");
      }
    }
  }
  
  Serial.println("Scan complete");
  return false;
}

// Загрузка калибровочных данных из EEPROM
bool loadCalibrationData() {
  EEPROM.get(0, calibData);
  
  // Проверка маркера валидности
  if (calibData.validMarker[0] == 'M' &&
      calibData.validMarker[1] == 'P' &&
      calibData.validMarker[2] == 'U' &&
      calibData.validMarker[3] == '6') {
    return true;
  }
  return false;
}

// Сохранение калибровочных данных в EEPROM
void saveCalibrationData() {
  // Обновляем маркер валидности
  calibData.validMarker[0] = 'M';
  calibData.validMarker[1] = 'P';
  calibData.validMarker[2] = 'U';
  calibData.validMarker[3] = '6';
  
  EEPROM.put(0, calibData);
  EEPROM.commit();
  Serial.println("   ✓ Calibration data saved to non-volatile memory");
}

// Калибровка гироскопа и акселерометра
void calibrateGyroAccel() {
  Serial.println("   Preparing for calibration...");
  Serial.println("   Keep the device stationary");
  
  for (int i = 3; i > 0; i--) {
    Serial.print("   ");
    Serial.println(i);
    delay(1000);
  }
  
  Serial.println("   Starting calibration... Do not move!");
  
  float sumGyroX = 0, sumGyroY = 0, sumGyroZ = 0;
  float sumAccelX = 0, sumAccelY = 0, sumAccelZ = 0;
  int calibration_samples = 2000;
  
  for (int i = 0; i < calibration_samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    
    sumGyroX += g.gyro.x;
    sumGyroY += g.gyro.y;
    sumGyroZ += g.gyro.z;
    
    sumAccelX += a.acceleration.x;
    sumAccelY += a.acceleration.y;
    // Ожидаемое значение Z ~9.8 м/с² (гравитация Земли)
    sumAccelZ += a.acceleration.z;
    
    if (i % 200 == 0) {
      Serial.print(".");
    }
    delay(1);
  }
  
  calibData.gyroX_offset = sumGyroX / calibration_samples;
  calibData.gyroY_offset = sumGyroY / calibration_samples;
  calibData.gyroZ_offset = sumGyroZ / calibration_samples;
  
  // Сохраняем смещения акселерометра
  calibData.accelX_offset = sumAccelX / calibration_samples;
  calibData.accelY_offset = sumAccelY / calibration_samples;
  calibData.accelZ_offset = (sumAccelZ / calibration_samples) - 9.81;
  
  // Обновляем текущие переменные
  gyroOffsetX = calibData.gyroX_offset;
  gyroOffsetY = calibData.gyroY_offset;
  gyroOffsetZ = calibData.gyroZ_offset;
  yawDrift = gyroOffsetZ;
  
  Serial.println("\n   ✓ Calibration complete!");
  Serial.print("   Gyro offsets: ");
  Serial.print("X="); Serial.print(calibData.gyroX_offset, 6);
  Serial.print(" Y="); Serial.print(calibData.gyroY_offset, 6);
  Serial.print(" Z="); Serial.println(calibData.gyroZ_offset, 6);
  
  Serial.print("   Accelerometer offsets: ");
  Serial.print("X="); Serial.print(calibData.accelX_offset, 6);
  Serial.print(" Y="); Serial.print(calibData.accelY_offset, 6);
  Serial.print(" Z="); Serial.println(calibData.accelZ_offset, 6);
}

// Установка текущей ориентации как нулевой
void setZeroOrientation() {
  Serial.println("   Setting current orientation as zero point...");
  
  // Получаем текущие углы от акселерометра
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  // Компенсируем смещения акселерометра
  float ax = a.acceleration.x - calibData.accelX_offset;
  float ay = a.acceleration.y - calibData.accelY_offset;
  float az = a.acceleration.z - calibData.accelZ_offset;
  
  // Рассчитываем углы от акселерометра
  float accel_pitch = atan2(ay, az) * 180 / PI;
  float accel_roll = atan2(-ax, sqrt(ay * ay + az * az)) * 180 / PI;
  
  // Сохраняем текущие углы как нулевые
  calibData.zero_pitch = accel_pitch;
  calibData.zero_roll = accel_roll;
  calibData.zero_yaw = 0; // Начальный Yaw устанавливаем в 0
  
  // Обновляем текущие переменные
  zeroPitch = calibData.zero_pitch;
  zeroRoll = calibData.zero_roll;
  zeroYaw = calibData.zero_yaw;
  
  // Сбрасываем текущие углы космического корабля
  spacecraft_pitch = 0;
  spacecraft_roll = 0;
  spacecraft_yaw = 0;
  
  // Сбрасываем smoothed значения
  smoothedPitch = 0;
  smoothedRoll = 0;
  smoothedYaw = 0;
  pitch = 0;
  roll = 0;
  yaw = 0;
  
  // Сбрасываем накопленные значения
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  
  // Сбрасываем continuous tracking
  contPitch = 0;
  contRoll = 0;
  contYaw = 0;
  prevAbsPitch = 0;
  prevAbsRoll = 0;
  prevAbsYaw = 0;
  prevContPitch = 0;
  prevContRoll = 0;
  prevContYaw = 0;
  firstMeasurement = true;
  
  // Сбрасываем gaze direction
  gazePitch = 0;
  gazeYaw = 0;
  gazeRoll = 0;
  
  // Сбрасываем direction
  pitchDirection = 0;
  rollDirection = 0;
  yawDirection = 0;
  
  Serial.print("   ✓ Zero point set: ");
  Serial.print("Pitch="); Serial.print(calibData.zero_pitch, 2);
  Serial.print("° Roll="); Serial.print(calibData.zero_roll, 2);
  Serial.println("° Yaw=0°");
}

// Обновление ориентации космического корабля (без ограничений)
void updateOrientation() {
  if (!calibrated) {
    calibrateGyro();
    return;
  }
  
  // Получаем данные с датчика
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    Serial.println("Error reading MPU6050 data");
    return;
  }
  
  // Компенсируем смещения гироскопа
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  // Компенсируем смещения акселерометра
  float ax = a.acceleration.x - calibData.accelX_offset;
  float ay = a.acceleration.y - calibData.accelY_offset;
  float az = a.acceleration.z - calibData.accelZ_offset;
  
  // Рассчитываем время
  unsigned long current_time = micros();
  float dt = (current_time - lastTime) / 1000000.0;
  lastTime = current_time;
  
  if (dt <= 0) dt = 0.0001; // Минимальное значение для избежания деления на ноль
  
  // Преобразование в градусы/секунду
  float gyro_pitch_rate = gyroY * (180.0 / PI);
  float gyro_roll_rate = gyroX * (180.0 / PI);
  float gyro_yaw_rate = gyroZ * (180.0 / PI);
  
  // Применяем компенсацию дрейфа yaw
  if (abs(gyroZ) < 0.01) {
    gyro_yaw_rate -= yawDrift * YAW_DRIFT_COMPENSATION * (180.0 / PI);
  }
  
  // Интегрируем угловые скорости для получения углов (БЕЗ ОГРАНИЧЕНИЙ)
  spacecraft_pitch += gyro_pitch_rate * dt;
  spacecraft_roll += gyro_roll_rate * dt;
  spacecraft_yaw += gyro_yaw_rate * dt;
  
  // Для Pitch и Roll используем комплементарный фильтр с акселерометром
  float accel_pitch = atan2(ay, az) * 180 / PI - calibData.zero_pitch;
  float accel_roll = atan2(-ax, sqrt(ay * ay + az * az)) * 180 / PI - calibData.zero_roll;
  
  float alpha = 0.96; // Больший вес гироскопу
  spacecraft_pitch = alpha * spacecraft_pitch + (1 - alpha) * accel_pitch;
  spacecraft_roll = alpha * spacecraft_roll + (1 - alpha) * accel_roll;
  
  // Обновляем smoothed значения для совместимости
  smoothedPitch = smoothedPitch * (1 - smoothingFactor) + spacecraft_pitch * smoothingFactor;
  smoothedRoll = smoothedRoll * (1 - smoothingFactor) + spacecraft_roll * smoothingFactor;
  smoothedYaw = smoothedYaw * (1 - smoothingFactor) + spacecraft_yaw * smoothingFactor;
  
  // Проверяем, не простаивает ли устройство
  isDeviceIdle = checkIfDeviceIdle();
  
  // Если устройство простаивает и установлена нулевая точка, добавляем Yaw
  if (isDeviceIdle && zeroSet) {
    handleIdleYawIncrement();
  }
  
  // Вычисляем направление взгляда
  calculateGazeDirection();
  
  // Обновляем накопленные углы
  updateAccumulatedAngles();
  
  // Отправляем данные через Serial с фиксированным интервалом
  unsigned long currentTime = millis();
  if (currentTime - lastSerialOutput >= SERIAL_OUTPUT_INTERVAL) {
    sendOrientationData(currentTime);
    lastSerialOutput = currentTime;
  }
}

void calculateGazeDirection() {
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    return;
  }
  
  float headMovement = sqrt(g.gyro.x * g.gyro.x + g.gyro.y * g.gyro.y + g.gyro.z * g.gyro.z);
  headMovementFiltered = headMovementFiltered * HEAD_MOVEMENT_SMOOTHING + 
                        headMovement * (1 - HEAD_MOVEMENT_SMOOTHING);
  
  float gazeSmoothing = 0.7;
  if (headMovementFiltered > 0.5) {
    gazeSmoothing = 0.3;
  }
  
  float targetGazePitch = constrain(smoothedPitch, -MAX_GAZE_PITCH, MAX_GAZE_PITCH);
  float targetGazeYaw = constrain(smoothedYaw, -MAX_GAZE_YAW, MAX_GAZE_YAW);
  
  gazePitch = gazePitch * gazeSmoothing + targetGazePitch * (1 - gazeSmoothing);
  gazeYaw = gazeYaw * gazeSmoothing + targetGazeYaw * (1 - gazeSmoothing);
  gazeRoll = gazeRoll * gazeSmoothing + smoothedRoll * (1 - gazeSmoothing);
  
  if (zeroSet) {
    gazePitch = gazePitch - zeroPitch;
    gazeYaw = gazeYaw - zeroYaw;
    gazeRoll = gazeRoll - zeroRoll;
  }
  
  gazePitch = constrain(gazePitch, -MAX_GAZE_PITCH, MAX_GAZE_PITCH);
  gazeYaw = constrain(gazeYaw, -MAX_GAZE_YAW, MAX_GAZE_YAW);
  
  while (gazeRoll > 180) gazeRoll -= 360;
  while (gazeRoll < -180) gazeRoll += 360;
}

bool checkIfDeviceIdle() {
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    return false;
  }
  
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  float gyroXDeg = gyroX * 180.0 / PI;
  float gyroYDeg = gyroY * 180.0 / PI;
  float gyroZDeg = gyroZ * 180.0 / PI;
  
  return (abs(gyroXDeg) < IDLE_THRESHOLD && 
          abs(gyroYDeg) < IDLE_THRESHOLD && 
          abs(gyroZDeg) < IDLE_THRESHOLD);
}

void handleIdleYawIncrement() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastIdleYawIncrement >= IDLE_YAW_INCREMENT_INTERVAL) {
    lastIdleYawIncrement = currentTime;
    
    // Добавляем Yaw в космический корабль (неограниченно)
    spacecraft_yaw += 1.0;
    smoothedYaw = smoothedYaw * (1 - smoothingFactor) + spacecraft_yaw * smoothingFactor;
    
    // Обновляем накопленный Yaw
    accumulatedYaw += 1.0;
    contYaw += 1.0;
    prevContYaw += 1.0;
    yawDirection = 1;
    
    // Для отладки (опционально)
    // Serial.printf("🔄 Idle Yaw increment: +1.0° | New spacecraft Yaw: %.1f°\n", spacecraft_yaw);
  }
}

float calculateRelativeAngle(float absoluteAngle, float zeroAngle) {
  float relative = absoluteAngle - zeroAngle;
  while (relative > 180) relative -= 360;
  while (relative < -180) relative += 360;
  return relative;
}

float calculateContinuousRelativeAngle(float absoluteAngle, float zeroAngle, float &prevAbsolute, float &continuousAngle) {
  if (firstMeasurement) {
    prevAbsolute = absoluteAngle;
    continuousAngle = absoluteAngle;
    return absoluteAngle - zeroAngle;
  }
  
  float delta = absoluteAngle - prevAbsolute;
  
  if (delta > 180) {
    delta -= 360;
  } else if (delta < -180) {
    delta += 360;
  }
  
  continuousAngle += delta;
  prevAbsolute = absoluteAngle;
  
  return continuousAngle - zeroAngle;
}

void updateAccumulatedAngles() {
  float continuousRelPitch = calculateContinuousRelativeAngle(smoothedPitch, zeroPitch, prevAbsPitch, contPitch);
  float continuousRelRoll = calculateContinuousRelativeAngle(smoothedRoll, zeroRoll, prevAbsRoll, contRoll);
  float continuousRelYaw = calculateContinuousRelativeAngle(smoothedYaw, zeroYaw, prevAbsYaw, contYaw);
  
  float deltaPitch = continuousRelPitch - prevContPitch;
  float deltaRoll = continuousRelRoll - prevContRoll;
  float deltaYaw = continuousRelYaw - prevContYaw;
  
  pitchDirection = (abs(deltaPitch) < 0.5) ? 0 : ((deltaPitch > 0) ? 1 : -1);
  rollDirection = (abs(deltaRoll) < 0.5) ? 0 : ((deltaRoll > 0) ? 1 : -1);
  yawDirection = (abs(deltaYaw) < 0.5) ? 0 : ((deltaYaw > 0) ? 1 : -1);
  
  accumulatedPitch = continuousRelPitch;
  accumulatedRoll = continuousRelRoll;
  accumulatedYaw = continuousRelYaw;
  
  prevContPitch = continuousRelPitch;
  prevContRoll = continuousRelRoll;
  prevContYaw = continuousRelYaw;
  
  firstMeasurement = false;
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
      Serial.printf("Calibration progress: %d%%\n", progress);
    }
  } else {
    gyroOffsetX = sumX / sampleCount;
    gyroOffsetY = sumY / sampleCount;
    gyroOffsetZ = sumZ / sampleCount;
    calibrated = true;
    
    yawDrift = gyroOffsetZ;
    
    Serial.println("✅ Gyro calibration complete!");
    Serial.printf("Offsets - X:%.6f, Y:%.6f, Z:%.6f\n", gyroOffsetX, gyroOffsetY, gyroOffsetZ);
    Serial.printf("Yaw drift compensation: %.6f\n", yawDrift);
    Serial.printf("Samples processed: %d\n", sampleCount);
    
    // Обновляем сохраненные данные
    calibData.gyroX_offset = gyroOffsetX;
    calibData.gyroY_offset = gyroOffsetY;
    calibData.gyroZ_offset = gyroOffsetZ;
  }
}

void sendOrientationData(unsigned long currentTime) {
  // Обновляем накопленные углы
  updateAccumulatedAngles();
  
  // Вычисляем относительные углы (для совместимости с визуализатором)
  float relPitch = calculateRelativeAngle(smoothedPitch, zeroPitch);
  float relRoll = calculateRelativeAngle(smoothedRoll, zeroRoll);
  float relYaw = calculateRelativeAngle(smoothedYaw, zeroYaw);
  
  // Создаем JSON сообщение
  String json = "{";
  json += "\"type\":\"sensorData\",";
  json += "\"pitch\":" + String(relPitch, 2) + ",";
  json += "\"roll\":" + String(relRoll, 2) + ",";
  json += "\"yaw\":" + String(relYaw, 2) + ",";
  json += "\"absPitch\":" + String(smoothedPitch, 2) + ",";
  json += "\"absRoll\":" + String(smoothedRoll, 2) + ",";
  json += "\"absYaw\":" + String(smoothedYaw, 2) + ",";
  // Космический корабль (неограниченные углы)
  json += "\"spacecraftPitch\":" + String(spacecraft_pitch, 2) + ",";
  json += "\"spacecraftRoll\":" + String(spacecraft_roll, 2) + ",";
  json += "\"spacecraftYaw\":" + String(spacecraft_yaw, 2) + ",";
  json += "\"accPitch\":" + String(accumulatedPitch, 2) + ",";
  json += "\"accRoll\":" + String(accumulatedRoll, 2) + ",";
  json += "\"accYaw\":" + String(accumulatedYaw, 2) + ",";
  json += "\"gazePitch\":" + String(gazePitch, 2) + ",";
  json += "\"gazeYaw\":" + String(gazeYaw, 2) + ",";
  json += "\"gazeRoll\":" + String(gazeRoll, 2) + ",";
  json += "\"dirPitch\":" + String(pitchDirection) + ",";
  json += "\"dirRoll\":" + String(rollDirection) + ",";
  json += "\"dirYaw\":" + String(yawDirection) + ",";
  json += "\"zeroPitch\":" + String(zeroPitch, 2) + ",";
  json += "\"zeroRoll\":" + String(zeroRoll, 2) + ",";
  json += "\"zeroYaw\":" + String(zeroYaw, 2) + ",";
  json += "\"zeroSet\":" + String(zeroSet ? "true" : "false") + ",";
  json += "\"idle\":" + String(isDeviceIdle ? "true" : "false") + ",";
  json += "\"timestamp\":" + String(currentTime);
  json += "}";
  
  Serial.println(json);
}

void checkSerialCommands() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "recalibrate") {
      calibrated = false;
      calibrationStart = millis();
      pitch = roll = yaw = 0;
      spacecraft_pitch = 0;
      spacecraft_roll = 0;
      spacecraft_yaw = 0;
      yawDrift = 0;
      firstMeasurement = true;
      Serial.println("{\"type\":\"status\",\"message\":\"Recalibrating gyro...\"}");
    } 
    else if (command == "setZero") {
      setZeroPoint();
    } 
    else if (command == "resetZero") {
      resetZeroPoint();
    } 
    else if (command == "resetGaze") {
      resetGazeDirection();
    } 
    else if (command == "resetAccumulated") {
      resetAccumulatedAngles();
    } 
    else if (command == "resetYaw") {
      resetYaw();
    }
  }
}

void setZeroPoint() {
  // Используем космический корабль для установки нуля
  zeroPitch = spacecraft_pitch;
  zeroRoll = spacecraft_roll;
  zeroYaw = spacecraft_yaw;
  zeroSet = true;
  
  // Обновляем сохраненные данные
  calibData.zero_pitch = spacecraft_pitch;
  calibData.zero_roll = spacecraft_roll;
  calibData.zero_yaw = spacecraft_yaw;
  
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  
  contPitch = spacecraft_pitch;
  contRoll = spacecraft_roll;
  contYaw = spacecraft_yaw;
  prevAbsPitch = spacecraft_pitch;
  prevAbsRoll = spacecraft_roll;
  prevAbsYaw = spacecraft_yaw;
  prevContPitch = 0;
  prevContRoll = 0;
  prevContYaw = 0;
  firstMeasurement = true;
  
  gazePitch = 0;
  gazeYaw = 0;
  gazeRoll = 0;
  
  pitchDirection = 0;
  rollDirection = 0;
  yawDirection = 0;
  
  saveCalibrationData();
  
  Serial.printf("{\"type\":\"zeroInfo\",\"zeroPitch\":%.2f,\"zeroRoll\":%.2f,\"zeroYaw\":%.2f}\n", 
                zeroPitch, zeroRoll, zeroYaw);
  Serial.println("{\"type\":\"status\",\"message\":\"Zero point set\"}");
}

void resetZeroPoint() {
  zeroPitch = 0;
  zeroRoll = 0;
  zeroYaw = 0;
  zeroSet = false;
  
  calibData.zero_pitch = 0;
  calibData.zero_roll = 0;
  calibData.zero_yaw = 0;
  
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  
  contPitch = 0;
  contRoll = 0;
  contYaw = 0;
  prevAbsPitch = 0;
  prevAbsRoll = 0;
  prevAbsYaw = 0;
  prevContPitch = 0;
  prevContRoll = 0;
  prevContYaw = 0;
  firstMeasurement = true;
  
  gazePitch = 0;
  gazeYaw = 0;
  gazeRoll = 0;
  
  pitchDirection = 0;
  rollDirection = 0;
  yawDirection = 0;
  
  saveCalibrationData();
  
  Serial.println("{\"type\":\"zeroReset\"}");
  Serial.println("{\"type\":\"status\",\"message\":\"Zero point reset\"}");
}

void resetGazeDirection() {
  gazePitch = 0;
  gazeYaw = 0;
  gazeRoll = 0;
  Serial.println("{\"type\":\"status\",\"message\":\"Gaze direction reset\"}");
}

void resetAccumulatedAngles() {
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  Serial.println("{\"type\":\"status\",\"message\":\"Accumulated angles reset\"}");
}

void resetYaw() {
  // Сбрасываем все значения Yaw
  yaw = 0;
  smoothedYaw = 0;
  spacecraft_yaw = 0;
  accumulatedYaw = 0;
  contYaw = 0;
  prevAbsYaw = 0;
  prevContYaw = 0;
  Serial.println("{\"type\":\"status\",\"message\":\"Yaw reset\"}");
}

// Вывод справки по командам
void printHelp() {
  Serial.println("\n=== COMMANDS ===");
  Serial.println("calib   - Calibrate sensors (gyro + accelerometer)");
  Serial.println("zero    - Set current orientation as zero");
  Serial.println("start   - Start real-time orientation output");
  Serial.println("stop    - Stop orientation output");
  Serial.println("save    - Save current settings");
  Serial.println("load    - Load settings from memory");
  Serial.println("status  - Show current settings");
  Serial.println("help    - Show this help");
  Serial.println("auto    - Perform automatic startup sequence");
  Serial.println("exit    - Exit command mode (start streaming data)");
  Serial.println("========================================");
}

// Обработка команд
void processCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  
  if (cmd == "calib") {
    calibrateGyroAccel();
    saveCalibrationData();
  } else if (cmd == "zero") {
    setZeroOrientation();
    saveCalibrationData();
  } else if (cmd == "start") {
    commandMode = false;
    Serial.println("\nStarting orientation monitoring...");
    Serial.println("Send 'c' to enter command mode");
    lastTime = micros(); // Сброс времени
    lastSerialOutput = millis();
  } else if (cmd == "stop") {
    commandMode = true;
    Serial.println("\nOrientation monitoring stopped");
  } else if (cmd == "save") {
    saveCalibrationData();
  } else if (cmd == "load") {
    if (loadCalibrationData()) {
      // Обновляем текущие переменные
      gyroOffsetX = calibData.gyroX_offset;
      gyroOffsetY = calibData.gyroY_offset;
      gyroOffsetZ = calibData.gyroZ_offset;
      yawDrift = gyroOffsetZ;
      
      zeroPitch = calibData.zero_pitch;
      zeroRoll = calibData.zero_roll;
      zeroYaw = calibData.zero_yaw;
      zeroSet = true;
      calibrated = true;
      
      Serial.println("Settings loaded from memory");
    } else {
      Serial.println("Error loading settings");
    }
  } else if (cmd == "status") {
    Serial.println("\n=== CURRENT SETTINGS ===");
    Serial.print("I2C Address: 0x");
    Serial.println(calibData.i2c_address, HEX);
    Serial.print("Zero point - Pitch: ");
    Serial.print(calibData.zero_pitch, 2);
    Serial.print("°, Roll: ");
    Serial.print(calibData.zero_roll, 2);
    Serial.print("°, Yaw: ");
    Serial.println(calibData.zero_yaw, 2);
    Serial.print("Gyro offsets - X: ");
    Serial.print(calibData.gyroX_offset, 6);
    Serial.print(", Y: ");
    Serial.print(calibData.gyroY_offset, 6);
    Serial.print(", Z: ");
    Serial.println(calibData.gyroZ_offset, 6);
    Serial.print("Spacecraft angles - Pitch: ");
    Serial.print(spacecraft_pitch, 2);
    Serial.print("°, Roll: ");
    Serial.print(spacecraft_roll, 2);
    Serial.print("°, Yaw: ");
    Serial.println(spacecraft_yaw, 2);
  } else if (cmd == "help") {
    printHelp();
  } else if (cmd == "auto") {
    Serial.println("\nPerforming automatic startup sequence...");
    performAutoStartSequence();
  } else if (cmd == "exit") {
    commandMode = false;
    Serial.println("\nExiting to streaming data mode...");
    lastTime = micros();
    lastSerialOutput = millis();
  } else if (cmd == "") {
    // Пустая команда - ничего не делаем
  } else {
    Serial.print("Unknown command: ");
    Serial.println(cmd);
    Serial.println("Enter 'help' for command list");
  }
}
