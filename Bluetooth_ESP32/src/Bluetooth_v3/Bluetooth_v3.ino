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

// Структура для данных с датчика
struct SensorData {
  float ax, ay, az;     // Ускорение (g)
  float gx, gy, gz;     // Гироскоп (°/s)
  float temperature;    // Температура (°C)
  unsigned long uptime; // Время работы (сек)
} sensorData;

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
      // Получаем значение как String (вместо std::string)
      String value = pCharacteristic->getValue();
      
      if (value.length() > 0) {
        Serial.print("Получено: ");
        Serial.println(value);
        
        String response = "";
        
        // Обработка команд
        if (value == "LED ON") {
          response = "LED включен";
          digitalWrite(2, HIGH);
        } 
        else if (value == "LED OFF") {
          response = "LED выключен";
          digitalWrite(2, LOW);
        } 
        else if (value == "STATUS") {
          response = "Статус: OK, Uptime: " + String(millis() / 1000) + "s";
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

// Чтение данных с MPU6050
void readMPU6050() {
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
    int16_t temp_raw = Wire.read() << 8 | Wire.read();
    
    // Чтение гироскопа
    int16_t gx_raw = Wire.read() << 8 | Wire.read();
    int16_t gy_raw = Wire.read() << 8 | Wire.read();
    int16_t gz_raw = Wire.read() << 8 | Wire.read();
    
    // Конвертация в физические величины
    // ±2g диапазон -> 16384 LSB/g
    sensorData.ax = ax_raw / 16384.0;
    sensorData.ay = ay_raw / 16384.0;
    sensorData.az = az_raw / 16384.0;
    
    // ±250 °/s диапазон -> 131 LSB/°/s
    sensorData.gx = gx_raw / 131.0;
    sensorData.gy = gy_raw / 131.0;
    sensorData.gz = gz_raw / 131.0;
    
    // Температура: 340 LSB/°C, смещение +36.53
    sensorData.temperature = temp_raw / 340.0 + 36.53;
    sensorData.uptime = millis() / 1000;
  }
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
  
  Serial.println("Запуск BLE сервера с MPU6050...");

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
  Serial.println("Доступные команды:");
  Serial.println("  LED ON/OFF - управление светодиодом");
  Serial.println("  STATUS - информация о системе");
  Serial.println("  TEMP - температура датчика");
  Serial.println("  DATA - все данные с датчика (формат CSV)");
  Serial.println("  ACCEL - только акселерометр");
  Serial.println("  GYRO - только гироскоп");
  Serial.println("  RESTART - перезагрузка ESP32");
}

void loop() {
  // Чтение данных с датчика
  readMPU6050();
  
  // Вывод в Serial для отладки
  static unsigned long lastSerialPrint = 0;
  if (millis() - lastSerialPrint > 1000) {
    Serial.print("A: ");
    Serial.print(sensorData.ax, 2); Serial.print("g, ");
    Serial.print(sensorData.ay, 2); Serial.print("g, ");
    Serial.print(sensorData.az, 2); Serial.print("g | ");
    
    Serial.print("G: ");
    Serial.print(sensorData.gx, 2); Serial.print("°/s, ");
    Serial.print(sensorData.gy, 2); Serial.print("°/s, ");
    Serial.print(sensorData.gz, 2); Serial.print("°/s | ");
    
    Serial.print("T: ");
    Serial.print(sensorData.temperature, 2); Serial.println("°C");
    
    lastSerialPrint = millis();
  }
  
  // Автоматическая отправка данных при подключении
  if (deviceConnected) {
    static unsigned long lastSendTime = 0;
    
    // Отправка данных каждые 100 мс
    if (millis() - lastSendTime > 100) {
      String dataString = String(sensorData.ax, 2) + "," + 
                         String(sensorData.ay, 2) + "," + 
                         String(sensorData.az, 2) + "," + 
                         String(sensorData.gx, 2) + "," + 
                         String(sensorData.gy, 2) + "," + 
                         String(sensorData.gz, 2) + "," + 
                         String(sensorData.temperature, 2);
      
      pCharacteristic->setValue(dataString.c_str());
      pCharacteristic->notify();
      lastSendTime = millis();
    }
  }
  
  // Обработка отключения/переподключения
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // даем время на отключение
    pServer->startAdvertising();
    Serial.println("Начато рекламирование...");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  delay(10); // Небольшая задержка для стабильности
}
