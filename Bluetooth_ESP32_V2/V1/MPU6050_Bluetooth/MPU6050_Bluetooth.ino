#include <Wire.h>
#include <MPU6050_light.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// UUID для службы и характеристики
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Дефолтные адреса I2C для MPU6050
#define MPU6050_DEFAULT_ADDRESS_1 0x68
#define MPU6050_DEFAULT_ADDRESS_2 0x69

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Sensor data
float pitch = 0, roll = 0, yaw = 0;
float lastSentPitch = 0, lastSentRoll = 0, lastSentYaw = 0;
bool calibrated = false;
unsigned long lastTime = 0;

// Относительный ноль
float zeroPitch = 0, zeroRoll = 0, zeroYaw = 0;
bool zeroSet = false;

// Накопленные углы (без ограничений)
double accumulatedPitch = 0, accumulatedRoll = 0, accumulatedYaw = 0;
float prevPitch = 0, prevRoll = 0, prevYaw = 0;
bool firstMeasurement = true;

// Таймер отправки данных
unsigned long lastDataSend = 0;
const unsigned long SEND_INTERVAL = 50;
const float CHANGE_THRESHOLD = 1.0;

// Переменные для хранения найденного адреса MPU6050
uint8_t mpuAddress = 0;
MPU6050 mpu(Wire);

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
  
  // Отправка через Serial
  String message = "ZERO_SET:PITCH:" + String(zeroPitch, 2) + 
                   ",ROLL:" + String(zeroRoll, 2) + 
                   ",YAW:" + String(zeroYaw, 2);
  Serial.println(message);
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
  Serial.println("ZERO_RESET");
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

// Улучшенная функция сканирования I2C
bool scanAndFindMPU6050() {
  byte error, address;
  int nDevices = 0;
  bool mpuFound = false;
  
  Serial.println("==========================================");
  Serial.println("Scanning I2C bus for MPU6050...");
  Serial.println("Scanning addresses 1-127...");
  
  Wire.begin();
  delay(100);
  
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("✓ I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.print(" (");
      Serial.print(address);
      Serial.print(")");
      
      // Проверяем, является ли это MPU6050 (адрес 0x68 или 0x69)
      if (address == MPU6050_DEFAULT_ADDRESS_1 || address == MPU6050_DEFAULT_ADDRESS_2) {
        Serial.println(" - MPU6050 detected!");
        mpuAddress = address;
        mpuFound = true;
      } else {
        Serial.println(" - Unknown device");
      }
      nDevices++;
    }
  }
  
  Serial.println("==========================================");
  
  if (nDevices == 0) {
    Serial.println("❌ No I2C devices found!");
    Serial.println("Please check I2C connections:");
    Serial.println("  SDA -> GPIO21 (ESP32)");
    Serial.println("  SCL -> GPIO22 (ESP32)");
    Serial.println("  VCC -> 3.3V");
    Serial.println("  GND -> GND");
    return false;
  } else {
    Serial.print("Found ");
    Serial.print(nDevices);
    Serial.println(" I2C device(s)");
  }
  
  if (!mpuFound) {
    Serial.println("❌ MPU6050 not found on I2C bus!");
    Serial.println("Checking specific MPU6050 addresses...");
    
    // Проверяем оба дефолтных адреса
    Serial.print("Checking address 0x68... ");
    Wire.beginTransmission(MPU6050_DEFAULT_ADDRESS_1);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.println("Found!");
      mpuAddress = MPU6050_DEFAULT_ADDRESS_1;
      mpuFound = true;
    } else {
      Serial.println("Not found");
    }
    
    Serial.print("Checking address 0x69... ");
    Wire.beginTransmission(MPU6050_DEFAULT_ADDRESS_2);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.println("Found!");
      mpuAddress = MPU6050_DEFAULT_ADDRESS_2;
      mpuFound = true;
    } else {
      Serial.println("Not found");
    }
  }
  
  if (mpuFound) {
    Serial.print("✅ MPU6050 found at address 0x");
    if (mpuAddress < 16) Serial.print("0");
    Serial.println(mpuAddress, HEX);
  }
  
  return mpuFound;
}

// Прямая проверка MPU6050 через регистры
bool checkMPU6050Directly(uint8_t address) {
  Serial.print("Direct checking MPU6050 at 0x");
  if (address < 16) Serial.print("0");
  Serial.print(address, HEX);
  Serial.println("...");
  
  // Попробуем прочитать регистр WHO_AM_I (0x75)
  Wire.beginTransmission(address);
  Wire.write(0x75); // WHO_AM_I register
  Wire.endTransmission(false);
  
  Wire.requestFrom(address, (uint8_t)1);
  if (Wire.available()) {
    uint8_t whoami = Wire.read();
    Serial.print("  WHO_AM_I register value: 0x");
    if (whoami < 16) Serial.print("0");
    Serial.println(whoami, HEX);
    
    // Для MPU6050 WHO_AM_I должен быть 0x68
    if (whoami == 0x68) {
      Serial.println("  ✅ Valid MPU6050 detected!");
      return true;
    } else {
      Serial.print("  ❌ Unexpected WHO_AM_I value. Expected 0x68, got 0x");
      if (whoami < 16) Serial.print("0");
      Serial.println(whoami, HEX);
      return false;
    }
  } else {
    Serial.println("  ❌ No response from device");
    return false;
  }
}

// Инициализация MPU6050 с библиотекой MPU6050_light
bool initMPU6050WithLightLibrary() {
  Serial.print("Initializing MPU6050 at address 0x");
  if (mpuAddress < 16) Serial.print("0");
  Serial.print(mpuAddress, HEX);
  Serial.println(" using MPU6050_light library...");
  
  // Сначала проверим через прямое обращение
  if (!checkMPU6050Directly(mpuAddress)) {
    Serial.println("❌ MPU6050 direct check failed!");
    return false;
  }
  
  // Инициализируем MPU6050 с найденным адресом
  byte status = mpu.begin(mpuAddress);
  
  if (status != 0) {
    Serial.print("❌ MPU6050 initialization failed! Error code: ");
    Serial.println(status);
    return false;
  }
  
  Serial.println("✅ MPU6050 initialized successfully!");
  
  // Даем время на стабилизацию
  delay(1000);
  
  // Калибруем гироскоп и акселерометр
  Serial.println("Calibrating MPU6050...");
  mpu.calcOffsets();
  calibrated = true;
  
  Serial.println("Calibration complete!");
  
  // Выводим информацию о калибровке
  Serial.println("Calibration offsets:");
  Serial.print("  Accel X: "); Serial.println(mpu.getAccXoffset());
  Serial.print("  Accel Y: "); Serial.println(mpu.getAccYoffset());
  Serial.print("  Accel Z: "); Serial.println(mpu.getAccZoffset());
  Serial.print("  Gyro X: "); Serial.println(mpu.getGyroXoffset());
  Serial.print("  Gyro Y: "); Serial.println(mpu.getGyroYoffset());
  Serial.print("  Gyro Z: "); Serial.println(mpu.getGyroZoffset());
  
  return true;
}

// Отправка данных через Serial и BLE
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
  
  // Отправка через Serial
  Serial.println(data);
  
  // Отправка через BLE если подключен клиент
  if (deviceConnected && pCharacteristic != NULL) {
    pCharacteristic->setValue(data.c_str());
    pCharacteristic->notify();
  }
  
  lastSentPitch = pitch;
  lastSentRoll = roll;
  lastSentYaw = yaw;
}

// Проверка изменений данных
bool dataChanged() {
  return (abs(pitch - lastSentPitch) >= CHANGE_THRESHOLD ||
          abs(roll - lastSentRoll) >= CHANGE_THRESHOLD ||
          abs(yaw - lastSentYaw) >= CHANGE_THRESHOLD);
}

// Обработка команд
void processCommand(String command) {
  Serial.print("Processing command: ");
  Serial.println(command);
  
  if (command == "GET_DATA") {
    sendSensorData();
  }
  else if (command == "RECALIBRATE") {
    calibrated = false;
    Serial.println("Recalibrating MPU6050...");
    mpu.calcOffsets();
    calibrated = true;
    Serial.println("RECALIBRATION_COMPLETE");
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue("RECALIBRATION_COMPLETE");
      pCharacteristic->notify();
    }
  }
  else if (command == "RESET_ANGLES") {
    pitch = 0; roll = 0; yaw = 0;
    lastSentPitch = 0; lastSentRoll = 0; lastSentYaw = 0;
    resetZeroPoint();
    Serial.println("ANGLES_RESET");
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue("ANGLES_RESET");
      pCharacteristic->notify();
    }
    sendSensorData();
  }
  else if (command == "SET_ZERO") {
    setZeroPoint();
    Serial.println("ZERO_POINT_SET");
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue("ZERO_POINT_SET");
      pCharacteristic->notify();
    }
  }
  else if (command == "RESET_ZERO") {
    resetZeroPoint();
    Serial.println("ZERO_POINT_RESET");
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue("ZERO_POINT_RESET");
      pCharacteristic->notify();
    }
  }
  else if (command == "LED ON") {
    // Включаем встроенный LED на ESP32
    digitalWrite(2, HIGH);
    Serial.println("LED turned ON");
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue("LED turned ON");
      pCharacteristic->notify();
    }
  }
  else if (command == "LED OFF") {
    // Выключаем встроенный LED
    digitalWrite(2, LOW);
    Serial.println("LED turned OFF");
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue("LED turned OFF");
      pCharacteristic->notify();
    }
  }
  else if (command == "STATUS") {
    String status = "Status: OK, Sensor: MPU6050";
    status += ", Address: 0x";
    if (mpuAddress < 16) status += "0";
    status += String(mpuAddress, HEX);
    status += ", Calibrated: " + String(calibrated ? "Yes" : "No");
    status += ", Uptime: " + String(millis() / 1000) + "s";
    Serial.println(status);
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue(status.c_str());
      pCharacteristic->notify();
    }
  }
  else if (command == "TEMP") {
    mpu.update();
    float temperature = mpu.getTemp();
    String tempStr = "Temperature: " + String(temperature) + "C";
    Serial.println(tempStr);
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue(tempStr.c_str());
      pCharacteristic->notify();
    }
  }
  else if (command == "SCAN_I2C") {
    scanAndFindMPU6050();
  }
  else if (command == "HELLO") {
    String response = "Hello from VR Head Tracker!";
    Serial.println(response);
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue(response.c_str());
      pCharacteristic->notify();
    }
  }
  else if (command == "TEST") {
    String response = "Test response from ESP32";
    Serial.println(response);
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue(response.c_str());
      pCharacteristic->notify();
    }
  }
  else if (command == "RESTART") {
    Serial.println("Restarting...");
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue("Restarting...");
      pCharacteristic->notify();
      delay(100);
    }
    
    ESP.restart();
  }
  else {
    // Эхо-ответ для неизвестных команд
    String response = "Unknown command: " + command;
    Serial.println(response);
    
    if (deviceConnected && pCharacteristic != NULL) {
      pCharacteristic->setValue(response.c_str());
      pCharacteristic->notify();
    }
  }
}

// Класс обратных вызовов BLE сервера
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

// Класс обратных вызовов BLE характеристики
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
        processCommand(value);
      }
    }
};

void setup() {
  Serial.begin(115200);
  delay(1000); // Даем время Serial для инициализации
  
  Serial.println("\n\n==========================================");
  Serial.println("=== VR Head Tracker with MPU6050 and BLE ===");
  Serial.println("==========================================");
  
  // Настройка встроенного LED для тестирования
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);
  
  // Инициализация I2C с другими пинами если нужно
  // Wire.begin(SDA_PIN, SCL_PIN); // Для ESP32 обычно 21, 22
  Wire.begin(21, 22); // Явно указываем пины для ESP32
  Wire.setClock(400000); // Устанавливаем скорость 400kHz
  
  // Сканирование I2C шины и поиск MPU6050
  bool mpuFound = scanAndFindMPU6050();
  
  if (!mpuFound) {
    Serial.println("\n❌ CRITICAL ERROR: MPU6050 not found!");
    Serial.println("\nTroubleshooting steps:");
    Serial.println("1. Check wiring connections:");
    Serial.println("   - VCC -> 3.3V (NOT 5V!)");
    Serial.println("   - GND -> GND");
    Serial.println("   - SCL -> GPIO22 (ESP32)");
    Serial.println("   - SDA -> GPIO21 (ESP32)");
    Serial.println("   - AD0 -> GND for address 0x68");
    Serial.println("   - AD0 -> 3.3V for address 0x69");
    Serial.println("\n2. Check pull-up resistors:");
    Serial.println("   - Add 4.7kΩ resistors between SDA/3.3V and SCL/3.3V");
    Serial.println("\n3. Try different I2C pins if available");
    
    // Мигаем LED для индикации ошибки
    for (int i = 0; i < 10; i++) {
      digitalWrite(2, HIGH);
      delay(100);
      digitalWrite(2, LOW);
      delay(100);
    }
    
    Serial.println("\nWill retry in 5 seconds...");
    delay(5000);
    ESP.restart();
  }
  
  // Инициализация MPU6050 с библиотекой MPU6050_light
  if (!initMPU6050WithLightLibrary()) {
    Serial.println("\n❌ Failed to initialize MPU6050 with MPU6050_light library!");
    
    // Пробуем альтернативный метод
    Serial.println("Trying alternative initialization method...");
    
    Wire.begin();
    delay(100);
    
    // Пробуем простую инициализацию
    mpu.begin();
    delay(1000);
    
    // Пробуем калибровку
    Serial.println("Trying to calibrate...");
    mpu.calcOffsets();
    
    // Проверяем, работает ли
    mpu.update();
    if (abs(mpu.getAngleX()) < 100 && abs(mpu.getAngleY()) < 100) {
      Serial.println("✅ MPU6050 working with alternative method!");
      calibrated = true;
    } else {
      Serial.println("❌ Alternative method also failed!");
      Serial.println("Restarting in 3 seconds...");
      delay(3000);
      ESP.restart();
    }
  }
  
  // Инициализация BLE
  Serial.println("\n==========================================");
  Serial.println("Starting BLE Server...");
  BLEDevice::init("VR_Head_Tracker");
  
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
  pCharacteristic->setValue("VR Head Tracker Ready");
  
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
  
  Serial.println("==========================================");
  Serial.println("✅ BLE Server ready!");
  Serial.println("📱 Device name: VR_Head_Tracker");
  Serial.print("🎯 MPU6050 address: 0x");
  if (mpuAddress < 16) Serial.print("0");
  Serial.println(mpuAddress, HEX);
  Serial.println("🔗 Service UUID: " + String(SERVICE_UUID));
  Serial.println("📊 Characteristic UUID: " + String(CHARACTERISTIC_UUID));
  Serial.println("⏳ Waiting for BLE connections...");
  
  Serial.println("\n📋 Available commands:");
  Serial.println("  LED ON/OFF       - Control built-in LED");
  Serial.println("  STATUS           - Get device status");
  Serial.println("  TEMP             - Get temperature");
  Serial.println("  SCAN_I2C         - Scan I2C bus");
  Serial.println("  GET_DATA         - Get sensor data");
  Serial.println("  RECALIBRATE      - Recalibrate sensor");
  Serial.println("  RESET_ANGLES     - Reset all angles");
  Serial.println("  SET_ZERO         - Set zero point");
  Serial.println("  RESET_ZERO       - Reset zero point");
  Serial.println("  HELLO/TEST       - Test commands");
  Serial.println("  RESTART          - Restart device");
  Serial.println("==========================================");
  
  // Мигаем LED 3 раза для индикации успешного запуска
  for (int i = 0; i < 3; i++) {
    digitalWrite(2, HIGH);
    delay(100);
    digitalWrite(2, LOW);
    delay(100);
  }
  
  lastDataSend = millis();
}

void loop() {
  // Обработка отключения/подключения BLE
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // даем время для завершения соединения
    pServer->startAdvertising(); // перезапускаем рекламу
    Serial.println("BLE advertising restarted");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  if (!calibrated) return;
  
  // Обновляем данные с MPU6050
  mpu.update();
  
  // Получаем углы из MPU6050
  pitch = mpu.getAngleX();  // Pitch (тангаж)
  roll = mpu.getAngleY();   // Roll (крен)
  yaw = mpu.getAngleZ();    // Yaw (рыскание)
  
  unsigned long currentTime = millis();
  
  // Отправка данных
  if (currentTime - lastDataSend >= SEND_INTERVAL) {
    if (dataChanged() || lastDataSend == 0) {
      sendSensorData();
      lastDataSend = currentTime;
    }
  }
  
  // Обработка Serial команд
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      processCommand(command);
    }
  }
  
  delay(10);
}
