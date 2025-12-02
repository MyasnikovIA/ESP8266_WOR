#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// UUID для службы и характеристики
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Device connected");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Device disconnected");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      // Получаем значение как std::string и конвертируем в String
      std::string stdValue = pCharacteristic->getValue();
      String value = "";
      
      // Конвертируем std::string в Arduino String
      for (int i = 0; i < stdValue.length(); i++) {
        value += stdValue[i];
      }
      
      if (value.length() > 0) {
        Serial.print("Received: ");
        Serial.println(value);
        
        // Обработка команд
        String response = "";
        if (value == "LED ON") {
          response = "LED turned ON";
          digitalWrite(2, HIGH); // Включаем встроенный LED (пин 2)
        } else if (value == "LED OFF") {
          response = "LED turned OFF";
          digitalWrite(2, LOW); // Выключаем встроенный LED
        } else if (value == "STATUS") {
          response = "Status: OK, Uptime: " + String(millis() / 1000) + "s";
        } else if (value == "TEMP") {
          // Имитация температуры
          float temp = 25.0 + (random(0, 100) / 100.0);
          response = "Temperature: " + String(temp) + "C";
        } else if (value == "RESTART") {
          response = "Restarting...";
          pCharacteristic->setValue(response.c_str());
          pCharacteristic->notify();
          delay(100);
          ESP.restart();
        } else {
          // Эхо-ответ
          response = "Echo: " + value;
        }
        
        // Отправляем ответ
        pCharacteristic->setValue(response.c_str());
        pCharacteristic->notify();
      }
    }
};

void setup() {
  Serial.begin(115200);
  
  // Настройка встроенного LED для тестирования
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);
  
  Serial.println("Starting BLE Server...");

  // Инициализация BLE
  BLEDevice::init("ESP32_BLE");
  
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
  pCharacteristic->setValue("ESP32 BLE Ready");
  
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
  
  Serial.println("BLE Server ready!");
  Serial.println("Device name: ESP32_BLE");
  Serial.println("Service UUID: " + String(SERVICE_UUID));
  Serial.println("Characteristic UUID: " + String(CHARACTERISTIC_UUID));
  Serial.println("Waiting for connections...");
  Serial.println("Use commands: LED ON, LED OFF, STATUS, TEMP, RESTART");
}

void loop() {
  // Автоматическая отправка статуса
  static unsigned long lastTime = 0;
  if (deviceConnected && (millis() - lastTime > 5000)) {
    String status = "Auto: Uptime " + String(millis() / 1000) + "s";
    pCharacteristic->setValue(status.c_str());
    pCharacteristic->notify();
    Serial.println("Auto-sent: " + status);
    lastTime = millis();
  }
  
  delay(100);
}
