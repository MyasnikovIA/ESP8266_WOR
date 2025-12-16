//lib   Adafruit MPU6050
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

// Переменные для комплементарного фильтра
float pitch = 0, roll = 0, yaw = 0;
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
bool calibrated = false;
unsigned long lastTime = 0;
unsigned long calibrationStart = 0;
const unsigned long calibrationTime = 3000;

// Переменные для сглаживания
float smoothedPitch = 0;
float smoothedRoll = 0;
float smoothedYaw = 0;
const float smoothingFactor = 0.3;

void setup(void) {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  // Инициализация MPU6050
  if (!mpu.begin()) {
    Serial.println("");
    Serial.println("Не удалось найти MPU6050芯片!");
    while (1) {
      delay(10);
    }
  }
  Serial.println("MPU6050 найден!");

  // Настройка параметров датчика
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);   // Диапазон акселерометра: ±8g
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);        // Диапазон гироскопа: ±500 град/с
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);     // Ширина полосы фильтра: 21 Гц

  Serial.println("🎯 Калибровка гироскопа... Держите датчик неподвижно 3 секунды!");
  calibrationStart = millis();

  delay(100);
}

void loop() {
  // Калибровка гироскопа
  if (!calibrated) {
    calibrateGyro();
    return;
  }

  // Получение новых данных с датчика
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Обработка данных сенсора
  processSensorData(a, g);

  // Вывод сырых данных акселерометра и гироскопа
  Serial.print("RAW Accel X: "); Serial.print(a.acceleration.x);
  Serial.print(", Y: "); Serial.print(a.acceleration.y);
  Serial.print(", Z: "); Serial.print(a.acceleration.z);
  Serial.print(" | Gyro X: "); Serial.print(g.gyro.x);
  Serial.print(", Y: "); Serial.print(g.gyro.y);
  Serial.print(", Z: "); Serial.print(g.gyro.z);

  // Вывод углов комплементарного фильтра
  Serial.print(" | COMP Pitch: "); Serial.print(smoothedPitch);
  Serial.print(", Roll: "); Serial.print(smoothedRoll);
  Serial.print(", Yaw: "); Serial.print(smoothedYaw);

  // Простой расчет углов наклона (Pitch и Roll) из акселерометра
  float accelPitch = atan2(a.acceleration.y, a.acceleration.z) * 180 / PI;
  float accelRoll = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180 / PI;

  Serial.print(" | ACCEL Pitch: "); Serial.print(accelPitch);
  Serial.print(" | Roll: "); Serial.println(accelRoll);

  delay(100); // Задержка для стабильности
}

void calibrateGyro() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  static int sampleCount = 0;
  static float sumX = 0, sumY = 0, sumZ = 0;
  
  if (millis() - calibrationStart < calibrationTime) {
    sumX += g.gyro.x;
    sumY += g.gyro.y;
    sumZ += g.gyro.z;
    sampleCount++;
    
    // Показать прогресс калибровки
    if (sampleCount % 50 == 0) {
      int progress = (millis() - calibrationStart) * 100 / calibrationTime;
      Serial.print("🔧 Калибровка: ");
      Serial.print(progress);
      Serial.println("%");
    }
  } else {
    gyroOffsetX = sumX / sampleCount;
    gyroOffsetY = sumY / sampleCount;
    gyroOffsetZ = sumZ / sampleCount;
    calibrated = true;
    
    Serial.println("✅ Калибровка гироскопа завершена!");
    Serial.print("📊 Смещения - X:");
    Serial.print(gyroOffsetX, 6);
    Serial.print(", Y:");
    Serial.print(gyroOffsetY, 6);
    Serial.print(", Z:");
    Serial.println(gyroOffsetZ, 6);
    Serial.print("📈 Обработано samples: ");
    Serial.println(sampleCount);
  }
}

void processSensorData(sensors_event_t a, sensors_event_t g) {
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  if (lastTime == 0) {
    deltaTime = 0.01; // Начальное маленькое значение
  }
  lastTime = currentTime;
  
  // Компенсация смещения гироскопа
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  // Расчет углов из акселерометра
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
  
  // Дополнительная стабилизация yaw когда устройство относительно стабильно
  float totalAccel = sqrt(a.acceleration.x * a.acceleration.x + 
                         a.acceleration.y * a.acceleration.y + 
                         a.acceleration.z * a.acceleration.z);
  
  // Если устройство относительно стабильно (не двигается сильно), применить небольшую коррекцию yaw
  if (abs(totalAccel - 9.8) < 0.5 && abs(gyroZ) < 0.005) {
    yaw *= 0.999; // Очень медленное затухание для предотвращения дрейфа
  }
  
  // Применение дополнительного сглаживания для отображения
  smoothedPitch = smoothedPitch * (1 - smoothingFactor) + pitch * smoothingFactor;
  smoothedRoll = smoothedRoll * (1 - smoothingFactor) + roll * smoothingFactor;
  smoothedYaw = smoothedYaw * (1 - smoothingFactor) + yaw * smoothingFactor;
}
