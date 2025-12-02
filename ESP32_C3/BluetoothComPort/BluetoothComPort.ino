#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <BluetoothSerial.h>

Adafruit_MPU6050 mpu;
BluetoothSerial BT;

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

// Пины I2C для ESP32
#define I2C_SDA 21
#define I2C_SCL 22

// Флаги для управления выводом данных
bool sendRawData = false;
bool sendCompactData = true;
bool sendProcessedData = true;

void setup(void) {
  Serial.begin(115200);
  
  // Инициализация Bluetooth
  if (!BT.begin("ESP32_MPU6050")) {
    Serial.println("❌ Ошибка инициализации Bluetooth!");
    while (1) {
      delay(1000);
    }
  }
  
  Serial.println("✅ Bluetooth устройство 'ESP32_MPU6050' готово к подключению");
  Serial.println("📱 Подключитесь к ESP32_MPU6050 через Bluetooth");

  // Инициализация I2C с указанием пинов
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Инициализация MPU6050
  if (!mpu.begin()) {
    Serial.println("❌ Не удалось найти MPU6050!");
    while (1) {
      delay(1000);
    }
  }
  
  Serial.println("✅ MPU6050 найден и инициализирован");

  // Настройка параметров датчика
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("🎯 Калибровка гироскопа... Держите датчик неподвижно 3 секунды!");
  BT.println("🎯 Калибровка гироскопа... Держите датчик неподвижно!");
  calibrationStart = millis();

  delay(100);
}

void loop() {
  // Обработка Bluetooth команд
  if (BT.available()) {
    String command = BT.readString();
    command.trim();
    handleBluetoothCommand(command);
  }

  // Калибровка гироскопа
  if (!calibrated) {
    calibrateGyro();
    return;
  }

  // Получение новых данных с датчика
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    Serial.println("❌ Ошибка чтения данных с MPU6050");
    return;
  }

  // Обработка данных сенсора
  processSensorData(a, g);

  // Отправка данных через Bluetooth
  sendBluetoothData(a, g);

  delay(50); // Задержка для стабильности
}

void sendBluetoothData(sensors_event_t a, sensors_event_t g) {
  // Компактные данные (основной формат)
  if (sendCompactData) {
    String compactData = "P:" + String(smoothedPitch, 1) + 
                        ",R:" + String(smoothedRoll, 1) + 
                        ",Y:" + String(smoothedYaw, 1);
    BT.println(compactData);
  }

  // Обработанные данные
  if (sendProcessedData) {
    String processedData = "ANGLES Pitch:" + String(smoothedPitch, 2) + 
                          "°, Roll:" + String(smoothedRoll, 2) + 
                          "°, Yaw:" + String(smoothedYaw, 2) + "°";
    // Отправляем обработанные данные реже, чтобы не засорять канал
    static unsigned long lastProcessedSend = 0;
    if (millis() - lastProcessedSend > 1000) {
      BT.println(processedData);
      lastProcessedSend = millis();
    }
  }

  // Сырые данные (по запросу)
  if (sendRawData) {
    String rawData = "RAW A_X:" + String(a.acceleration.x, 2) +
                    ",A_Y:" + String(a.acceleration.y, 2) +
                    ",A_Z:" + String(a.acceleration.z, 2) +
                    " G_X:" + String(g.gyro.x, 4) +
                    ",G_Y:" + String(g.gyro.y, 4) +
                    ",G_Z:" + String(g.gyro.z, 4);
    BT.println(rawData);
  }
}

void calibrateGyro() {
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    return;
  }
  
  static int sampleCount = 0;
  static float sumX = 0, sumY = 0, sumZ = 0;
  
  if (millis() - calibrationStart < calibrationTime) {
    sumX += g.gyro.x;
    sumY += g.gyro.y;
    sumZ += g.gyro.z;
    sampleCount++;
    
    // Показать прогресс калибровки
    if (sampleCount % 30 == 0) {
      int progress = (millis() - calibrationStart) * 100 / calibrationTime;
      String progressMsg = "🔧 Калибровка: " + String(progress) + "%";
      Serial.println(progressMsg);
      BT.println(progressMsg);
    }
  } else {
    // Завершение калибровки
    gyroOffsetX = sumX / sampleCount;
    gyroOffsetY = sumY / sampleCount;
    gyroOffsetZ = sumZ / sampleCount;
    calibrated = true;
    
    String calibComplete = "✅ Калибровка завершена! Смещения - " +
                          String(gyroOffsetX, 6) + "," +
                          String(gyroOffsetY, 6) + "," +
                          String(gyroOffsetZ, 6);
    Serial.println(calibComplete);
    BT.println(calibComplete);
    
    // Отправка справки по командам
    BT.println("💡 Введите HELP для списка команд");
  }
}

void processSensorData(sensors_event_t a, sensors_event_t g) {
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  
  if (lastTime == 0 || deltaTime > 0.1) {
    deltaTime = 0.01; // Защита от больших deltaTime
  }
  lastTime = currentTime;
  
  // Компенсация смещения гироскопа
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  // Расчет углов из акселерометра
  float accelPitch = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
  float accelRoll = atan2(-a.acceleration.x, 
                         sqrt(a.acceleration.y * a.acceleration.y + 
                              a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
  
  // Интеграция гироскопа (градусы/сек в градусы)
  pitch += gyroX * deltaTime * 180.0 / PI;
  roll += gyroY * deltaTime * 180.0 / PI;
  yaw += gyroZ * deltaTime * 180.0 / PI;
  
  // Комплементарный фильтр
  float alpha = 0.96;
  pitch = alpha * pitch + (1.0 - alpha) * accelPitch;
  roll = alpha * roll + (1.0 - alpha) * accelRoll;
  
  // Стабилизация yaw при отсутствии движения
  float totalAccel = sqrt(a.acceleration.x * a.acceleration.x + 
                         a.acceleration.y * a.acceleration.y + 
                         a.acceleration.z * a.acceleration.z);
  
  if (abs(totalAccel - 9.8) < 0.5 && abs(gyroZ) < 0.005) {
    yaw *= 0.999;
  }
  
  // Сглаживание для отображения
  smoothedPitch = smoothedPitch * (1 - smoothingFactor) + pitch * smoothingFactor;
  smoothedRoll = smoothedRoll * (1 - smoothingFactor) + roll * smoothingFactor;
  smoothedYaw = smoothedYaw * (1 - smoothingFactor) + yaw * smoothingFactor;
}

void handleBluetoothCommand(String command) {
  command.toUpperCase();
  
  if (command == "RESET" || command == "R") {
    resetOrientation();
    BT.println("✅ Ориентация сброшена");
  } 
  else if (command == "CALIBRATE" || command == "CAL") {
    recalibrate();
    BT.println("✅ Перекалибровка начата");
  }
  else if (command == "STATUS" || command == "S") {
    sendStatus();
  }
  else if (command == "HELP" || command == "H") {
    showHelp();
  }
  else if (command == "ANGLES" || command == "A") {
    sendAngles();
  }
  else if (command == "RAW ON") {
    sendRawData = true;
    BT.println("✅ Сырые данные включены");
  }
  else if (command == "RAW OFF") {
    sendRawData = false;
    BT.println("✅ Сырые данные выключены");
  }
  else if (command == "COMPACT ON") {
    sendCompactData = true;
    BT.println("✅ Компактные данные включены");
  }
  else if (command == "COMPACT OFF") {
    sendCompactData = false;
    BT.println("✅ Компактные данные выключены");
  }
  else if (command == "TEST") {
    BT.println("✅ Тестовая команда выполнена");
    BT.println("📡 Соединение работает нормально");
  }
  else {
    BT.println("❌ Неизвестная команда: " + command);
    BT.println("💡 Введите HELP для списка команд");
  }
}

void resetOrientation() {
  pitch = 0;
  roll = 0;
  yaw = 0;
  smoothedPitch = 0;
  smoothedRoll = 0;
  smoothedYaw = 0;
}

void recalibrate() {
  calibrated = false;
  calibrationStart = millis();
  gyroOffsetX = 0;
  gyroOffsetY = 0;
  gyroOffsetZ = 0;
  BT.println("🔄 Перекалибровка... Держите неподвижно 3 секунды");
}

void sendStatus() {
  String status = "📊 Статус системы:\n";
  status += "Датчик: MPU6050 ✅\n";
  status += "Калибровка: " + String(calibrated ? "✅ Завершена" : "⏳ В процессе") + "\n";
  status += "Смещения: " + String(gyroOffsetX, 6) + "," + String(gyroOffsetY, 6) + "," + String(gyroOffsetZ, 6) + "\n";
  status += "Данные: Compact-" + String(sendCompactData ? "ON" : "OFF") + 
            " Raw-" + String(sendRawData ? "ON" : "OFF");
  BT.println(status);
}

void showHelp() {
  String help = "📖 ДОСТУПНЫЕ КОМАНДЫ:\n";
  help += "HELP/H    - Эта справка\n";
  help += "RESET/R   - Сброс ориентации\n";
  help += "CALIBRATE/CAL - Перекалибровка\n";
  help += "STATUS/S  - Статус системы\n";
  help += "ANGLES/A  - Текущие углы\n";
  help += "RAW ON/OFF - Сырые данные\n";
  help += "COMPACT ON/OFF - Компактные данные\n";
  help += "TEST      - Тест связи";
  BT.println(help);
}

void sendAngles() {
  String angles = "📐 ТЕКУЩИЕ УГЛЫ:\n";
  angles += "Pitch: " + String(smoothedPitch, 2) + "°\n";
  angles += "Roll:  " + String(smoothedRoll, 2) + "°\n";
  angles += "Yaw:   " + String(smoothedYaw, 2) + "°";
  BT.println(angles);
}
