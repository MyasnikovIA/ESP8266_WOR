#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <BluetoothSerial.h>

Adafruit_MPU6050 mpu;
BluetoothSerial BT;

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

// Data sending management
unsigned long lastDataSend = 0;
const unsigned long SEND_INTERVAL = 50; // Send every 50ms
const float CHANGE_THRESHOLD = 0.5;

// Буфер для входящих команд
String inputBuffer = "";

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
  
  BT.println("ZERO_SET:PITCH:" + String(zeroPitch, 2) + 
             ",ROLL:" + String(zeroRoll, 2) + 
             ",YAW:" + String(zeroYaw, 2));
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
  BT.println("ZERO_RESET");
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
  Serial.println("Calibrating MPU6050...");
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
  Serial.print("Offsets - X: "); Serial.print(gyroOffsetX, 6);
  Serial.print(" Y: "); Serial.print(gyroOffsetY, 6);
  Serial.print(" Z: "); Serial.println(gyroOffsetZ, 6);
}

void sendSensorData() {
  // Обновляем накопленные углы
  updateAccumulatedAngles();
  
  // Получаем относительные углы
  double relPitch = getRelativePitch();
  double relRoll = getRelativeRoll();
  double relYaw = getRelativeYaw();
  
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
  
  BT.println(data);
  
  // Также выводим в Serial для отладки
  Serial.println("BT Sent: " + data);
}

bool dataChanged() {
  return (abs(pitch - lastSentPitch) >= CHANGE_THRESHOLD ||
          abs(roll - lastSentRoll) >= CHANGE_THRESHOLD ||
          abs(yaw - lastSentYaw) >= CHANGE_THRESHOLD);
}

void processCommand(String command) {
  command.trim();
  Serial.println("Received command: " + command);
  
  if (command == "GET_DATA") {
    sendSensorData();
  }
  else if (command == "RECALIBRATE") {
    calibrated = false;
    calibrateSensor();
    BT.println("RECALIBRATION_COMPLETE");
  }
  else if (command == "RESET_ANGLES") {
    pitch = 0; roll = 0; yaw = 0;
    lastSentPitch = 0; lastSentRoll = 0; lastSentYaw = 0;
    resetZeroPoint();
    BT.println("ANGLES_RESET");
    sendSensorData();
  }
  else if (command == "SET_ZERO") {
    setZeroPoint();
    BT.println("ZERO_POINT_SET");
  }
  else if (command == "RESET_ZERO") {
    resetZeroPoint();
    BT.println("ZERO_POINT_RESET");
  }
  else if (command == "STATUS") {
    BT.println("STATUS:MPU6050_Connected,Calibrated:" + String(calibrated ? "true" : "false"));
  }
  else if (command == "HELP") {
    BT.println("HELP:Available commands: GET_DATA, RECALIBRATE, RESET_ANGLES, SET_ZERO, RESET_ZERO, STATUS, HELP");
  }
  else {
    BT.println("ERROR:UNKNOWN_COMMAND:" + command);
  }
}

void readBluetoothData() {
  while (BT.available()) {
    char c = BT.read();
    
    if (c == '\n') {
      // Завершение команды
      if (inputBuffer.length() > 0) {
        processCommand(inputBuffer);
        inputBuffer = "";
      }
    } else if (c != '\r') {
      // Добавляем символ в буфер (игнорируем \r)
      inputBuffer += c;
    }
    
    // Защита от переполнения буфера
    if (inputBuffer.length() > 100) {
      inputBuffer = "";
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Инициализация Bluetooth
  BT.begin("VR_Head_BT");
  Serial.println("Bluetooth started! Device name: VR_Head_BT");
  Serial.println("Connect to this device and send commands:");
  Serial.println("GET_DATA, RECALIBRATE, RESET_ANGLES, SET_ZERO, RESET_ZERO, STATUS, HELP");
  
  // Инициализация MPU6050
  Wire.begin();
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip!");
    BT.println("ERROR:MPU6050 not found");
    while (1) {
      delay(1000);
    }
  }
  
  Serial.println("MPU6050 Found!");
  BT.println("STATUS:MPU6050 Found");
  
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
  
  calibrateSensor();
  
  Serial.println("System ready!");
  BT.println("SYSTEM_READY");
  
  lastTime = millis();
  lastDataSend = millis();
}

void loop() {
  // Чтение команд из Bluetooth
  readBluetoothData();
  
  if (!calibrated) {
    delay(100);
    return;
  }
  
  // Чтение данных с датчика
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  if (lastTime == 0) deltaTime = 0.01;
  lastTime = currentTime;
  
  // Компенсация смещения гироскопа
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  // Расчет углов по акселерометру
  float accelPitch = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
  float accelRoll = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
  
  // Интегрирование гироскопа
  pitch += gyroX * deltaTime * 180.0 / PI;
  roll += gyroY * deltaTime * 180.0 / PI;
  yaw += gyroZ * deltaTime * 180.0 / PI;
  
  // Комплементарный фильтр
  float alpha = 0.96;
  pitch = alpha * pitch + (1.0 - alpha) * accelPitch;
  roll = alpha * roll + (1.0 - alpha) * accelRoll;
  
  // Ограничение углов для отображения
  if (pitch > 180) pitch = 180;
  if (pitch < -180) pitch = -180;
  if (roll > 180) roll = 180;
  if (roll < -180) roll = -180;
  if (yaw > 180) yaw = 180;
  if (yaw < -180) yaw = -180;
  
  // Отправка данных по Bluetooth
  if (currentTime - lastDataSend >= SEND_INTERVAL) {
    if (dataChanged() || lastDataSend == 0) {
      sendSensorData();
      lastDataSend = currentTime;
    }
  }
  
  delay(10);
}
