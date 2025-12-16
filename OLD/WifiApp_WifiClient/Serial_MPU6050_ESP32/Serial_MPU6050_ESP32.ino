#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

// Смещения для калибровки
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
float accelOffsetX = 0, accelOffsetY = 0, accelOffsetZ = 0;

void calibrateSensor() {
  Serial.println("Калибровка... Не двигайте датчик!");
  delay(3000);
  
  float gx = 0, gy = 0, gz = 0;
  float ax = 0, ay = 0, az = 0;
  const int samples = 500;
  
  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    
    gx += g.gyro.x;
    gy += g.gyro.y;
    gz += g.gyro.z;
    
    ax += a.acceleration.x;
    ay += a.acceleration.y;
    az += a.acceleration.z - 9.81; // Вычитаем гравитацию по Z
    
    delay(10);
  }
  
  gyroOffsetX = gx / samples;
  gyroOffsetY = gy / samples;
  gyroOffsetZ = gz / samples;
  
  accelOffsetX = ax / samples;
  accelOffsetY = ay / samples;
  accelOffsetZ = az / samples;
  
  Serial.println("Калибровка завершена!");
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  if (!mpu.begin()) {
    Serial.println("MPU6050 не найден!");
    while(1);
  }
  
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  
  calibrateSensor();
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  // Применяем калибровку
  float calibratedAccelX = a.acceleration.x - accelOffsetX;
  float calibratedAccelY = a.acceleration.y - accelOffsetY;
  float calibratedAccelZ = a.acceleration.z - accelOffsetZ;
  
  float calibratedGyroX = g.gyro.x - gyroOffsetX;
  float calibratedGyroY = g.gyro.y - gyroOffsetY;
  float calibratedGyroZ = g.gyro.z - gyroOffsetZ;
  
  Serial.print("Calibrated Accel - X:"); Serial.print(calibratedAccelX);
  Serial.print(" Y:"); Serial.print(calibratedAccelY);
  Serial.print(" Z:"); Serial.println(calibratedAccelZ);
  
  delay(200);
}
