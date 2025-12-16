#include <Wire.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Используем библиотеку MPU6050_light
#include "MPU6050_light.h"

// UUID для BLE службы и характеристики
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// BLE переменные
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool bleDeviceConnected = false;
bool bleEnabled = true;

// MPU6050
MPU6050 mpu(Wire);
Preferences preferences;

// Структура для хранения калибровочных данных
struct CalibrationData {
  float gyro_offset_x, gyro_offset_y, gyro_offset_z;
  float accel_offset_x, accel_offset_y, accel_offset_z;
  float gyro_drift_threshold;
  int calibration_samples;
};

// Структура для нулевой точки
struct ZeroPoint {
  float roll_zero, pitch_zero, yaw_zero;
  float accel_x_zero, accel_y_zero, accel_z_zero;
};

CalibrationData calib_data;
ZeroPoint zero_point;

// Текущие углы
float roll = 0, pitch = 0, yaw = 0;
float filtered_roll = 0, filtered_pitch = 0, filtered_yaw = 0;

// Время для интеграции
unsigned long last_time = 0;
unsigned long last_calibration_check = 0;
unsigned long last_ble_update = 0;
const unsigned long CALIBRATION_CHECK_INTERVAL = 5000;
const unsigned long BLE_UPDATE_INTERVAL = 50; // 20 Гц для BLE

// Флаги состояния
bool is_calibrated = false;
bool zero_point_set = false;
bool auto_calibration_enabled = true;

// Фильтр для данных
const float ALPHA = 0.2;
bool mpu_initialized = false;

// Для сглаживания
#define SMOOTHING_WINDOW 5
float roll_history[SMOOTHING_WINDOW];
float pitch_history[SMOOTHING_WINDOW];
float yaw_history[SMOOTHING_WINDOW];
int history_index = 0;

// Переменные для определения адреса
uint8_t mpu_address = 0x68;

// Для компенсации дрейфа по всем осям
float roll_drift_accumulator = 0;
float pitch_drift_accumulator = 0;
float yaw_drift_accumulator = 0;
float roll_drift_correction = 0;
float pitch_drift_correction = 0;
float yaw_drift_correction = 0;
unsigned long last_still_check = 0;
const unsigned long STILL_CHECK_INTERVAL = 1000;
const float DRIFT_COMPENSATION_RATE = 0.001; // Скорость компенсации дрейфа
const float DRIFT_THRESHOLD = 0.05; // Порог для обнаружения дрейфа (град/сек)

// Прототипы функций
void initBLE();
String processCommand(String command);
String getSettingsString();
String getRawDataString();
void calibrateMPU();
void saveMPUCalibration();
void setZeroPoint();
void resetZeroPoint();
void loadCalibrationData();
void saveCalibrationData();
void loadZeroPoint();
void saveZeroPoint();
void checkAndCorrectDrift();
void scanI2C();
void setupMPU();
void handleSerialCommands();
void compensateAllAxisDrift();

// Класс для обработки событий BLE сервера
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      bleDeviceConnected = true;
      Serial.println("BLE устройство подключено");
    };

    void onDisconnect(BLEServer* pServer) {
      bleDeviceConnected = false;
      Serial.println("BLE устройство отключено");
      // Перезапускаем рекламу для новых подключений
      pServer->startAdvertising();
      Serial.println("Ожидание подключений...");
    }
};

// Класс для обработки команд через BLE
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      // Получаем значение как строку
      String value = "";
      
      // В ESP32 BLE getValue() возвращает std::string
      std::string stdValue = pCharacteristic->getValue();
      
      // Конвертируем std::string в Arduino String
      for (int i = 0; i < stdValue.length(); i++) {
        value += (char)stdValue[i];
      }
      
      if (value.length() > 0) {
        Serial.print("Получена BLE команда: ");
        Serial.println(value);
        
        // Обработка команд
        String response = processCommand(value);
        
        // Отправляем ответ
        pCharacteristic->setValue(response.c_str());
        pCharacteristic->notify();
      }
    }
};

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  
  Serial.println("VR Head Tracker with MPU6050 and BLE");
  Serial.println("Инициализация...");
  
  // Инициализация BLE
  if (bleEnabled) {
    initBLE();
  }
  
  // Инициализация I2C с указанием пинов
  Wire.begin(21, 22, 100000); // SDA=21, SCL=22, 100kHz
  
  // Сканирование I2C для подтверждения
  scanI2C();
  
  Serial.println("\nКоманды через Serial или BLE:");
  Serial.println("  'c' - калибровка датчика");
  Serial.println("  'z' - установить нулевую точку");
  Serial.println("  'r' - сбросить нулевую точку");
  Serial.println("  'a' - вкл/выкл авто-калибровку");
  Serial.println("  's' - показать настройки");
  Serial.println("  'd' - показать сырые данные");
  Serial.println("  'i' - информация о подключении");
  Serial.println("  't' - сканировать I2C шину");
  Serial.println("  'b' - вкл/выкл BLE");
  Serial.println("  'h' - показать это меню");
  Serial.println("  'y' - сбросить дрейф по всем осям");
  Serial.println("  'x' - сбросить дрейф Roll (X)");
  Serial.println("  'p' - сбросить дрейф Pitch (Y)");
  Serial.println("  'w' - сбросить дрейф Yaw (Z)");

  // Инициализация энергонезависимой памяти
  preferences.begin("mpu6050", false);
  
  // Загрузка калибровочных данных
  loadCalibrationData();
  // Загрузка нулевой точки
  loadZeroPoint();

  // Инициализация истории для сглаживания
  for (int i = 0; i < SMOOTHING_WINDOW; i++) {
    roll_history[i] = 0;
    pitch_history[i] = 0;
    yaw_history[i] = 0;
  }

  // Попытка инициализации MPU6050 по адресу 0x68
  Serial.println("\nИнициализация MPU6050 по адресу 0x68...");
  
  byte status = mpu.begin();
  Serial.print("Статус инициализации MPU6050: ");
  Serial.println(status);
  
  if (status == 0) {
    Serial.println("MPU6050 успешно найден по адресу 0x68!");
    mpu_initialized = true;
    
    // Задержка для стабилизации датчика
    delay(1000);
    
    // Выполняем калибровку, если еще не калиброван
    if (!is_calibrated) {
      Serial.println("Выполняем начальную калибровку...");
      Serial.println("Держите устройство неподвижно на ровной поверхности!");
      
      // Калибровка гироскопа и акселерометра
      mpu.calcOffsets();
      delay(2000);
      
      // Сохраняем калибровочные данные
      saveMPUCalibration();
      is_calibrated = true;
      Serial.println("Начальная калибровка завершена!");
    } else {
      Serial.println("Используем сохраненные калибровочные данные");
    }
    
    Serial.println("\nMPU6050 готов к работе!");
    Serial.println("Отправьте 'c' для точной калибровки");
    Serial.println("Отправьте 'z' для установки нулевой точки");
    
  } else {
    Serial.println("Ошибка инициализации MPU6050 по адресу 0x68!");
    Serial.println("Код ошибки: " + String(status));
    
    // Пробуем альтернативный адрес 0x69
    Serial.println("\nПробуем альтернативный адрес 0x69...");
    delay(100);
    
    // Переинициализируем Wire для нового адреса
    Wire.end();
    delay(100);
    Wire.begin(21, 22, 100000);
    delay(100);
    
    mpu_address = 0x69;
    
    // Проверяем подключение к адресу 0x69
    Wire.beginTransmission(0x69);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.println("Устройство найдено по адресу 0x69, пробуем инициализировать...");
      
      // Пересоздаем объект MPU6050
      mpu = MPU6050(Wire);
      
      status = mpu.begin();
      if (status == 0) {
        Serial.println("MPU6050 успешно инициализирован по адресу 0x69!");
        mpu_initialized = true;
        
        delay(1000);
        
        if (!is_calibrated) {
          Serial.println("Выполняем калибровку...");
          mpu.calcOffsets();
          delay(2000);
          saveMPUCalibration();
          is_calibrated = true;
        }
      } else {
        Serial.println("Не удалось инициализировать MPU6050 по адресу 0x69");
      }
    } else {
      Serial.println("Устройство не найдено по адресу 0x69");
    }
  }
  
  last_time = millis();
  last_ble_update = millis();
  last_still_check = millis();
}

void loop() {
  // Если MPU6050 не инициализирован, только обрабатываем команды
  if (!mpu_initialized) {
    handleSerialCommands();
    delay(100);
    return;
  }
  
  // Обновление данных с MPU6050
  mpu.update();
  
  unsigned long current_time = millis();
  float dt = (current_time - last_time) / 1000.0; // В секундах
  
  if (dt > 0.1) dt = 0.1; // Ограничиваем максимальный интервал
  last_time = current_time;
  
  // Получаем текущие углы из библиотеки
  float current_roll = mpu.getAngleX();
  float current_pitch = mpu.getAngleY();
  float current_yaw = mpu.getAngleZ();
  
  // Сглаживание данных
  roll_history[history_index] = current_roll;
  pitch_history[history_index] = current_pitch;
  yaw_history[history_index] = current_yaw;
  history_index = (history_index + 1) % SMOOTHING_WINDOW;
  
  // Вычисляем среднее значение
  float avg_roll = 0, avg_pitch = 0, avg_yaw = 0;
  for (int i = 0; i < SMOOTHING_WINDOW; i++) {
    avg_roll += roll_history[i];
    avg_pitch += pitch_history[i];
    avg_yaw += yaw_history[i];
  }
  avg_roll /= SMOOTHING_WINDOW;
  avg_pitch /= SMOOTHING_WINDOW;
  avg_yaw /= SMOOTHING_WINDOW;
  
  // Фильтрация
  filtered_roll = ALPHA * avg_roll + (1 - ALPHA) * filtered_roll;
  filtered_pitch = ALPHA * avg_pitch + (1 - ALPHA) * filtered_pitch;
  filtered_yaw = ALPHA * avg_yaw + (1 - ALPHA) * filtered_yaw;
  
  // Компенсация дрейфа по всем осям
  compensateAllAxisDrift();
  
  // Применение нулевой точки
  float roll_display, pitch_display, yaw_display;
  
  if (zero_point_set) {
    roll_display = filtered_roll - zero_point.roll_zero + roll_drift_correction;
    pitch_display = filtered_pitch - zero_point.pitch_zero + pitch_drift_correction;
    yaw_display = filtered_yaw - zero_point.yaw_zero + yaw_drift_correction;
  } else {
    roll_display = filtered_roll + roll_drift_correction;
    pitch_display = filtered_pitch + pitch_drift_correction;
    yaw_display = filtered_yaw + yaw_drift_correction;
  }
  
  // Формируем строку с данными
  String dataString = "ROLL:" + String(roll_display, 2) + 
                     ",PITCH:" + String(pitch_display, 2) + 
                     ",YAW:" + String(yaw_display, 2);
  
  // Отправка данных в серийный порт
  if (Serial.available() == 0) {
    Serial.println(dataString);
  }
  
  // Отправка данных через BLE
  if (bleEnabled && bleDeviceConnected && (current_time - last_ble_update >= BLE_UPDATE_INTERVAL)) {
    if (pCharacteristic != NULL) {
      pCharacteristic->setValue(dataString.c_str());
      pCharacteristic->notify();
      last_ble_update = current_time;
    }
  }
  
  // Автоматическая проверка и коррекция дрейфа
  if (auto_calibration_enabled && millis() - last_calibration_check > CALIBRATION_CHECK_INTERVAL) {
    checkAndCorrectDrift();
    last_calibration_check = millis();
  }
  
  // Обработка команд из серийного порта
  handleSerialCommands();
  
  delay(20); // 50 Гц обновление
}

// Компенсация дрейфа по всем осям
void compensateAllAxisDrift() {
  unsigned long current_time = millis();
  
  // Проверяем каждую секунду, не движется ли устройство
  if (current_time - last_still_check >= STILL_CHECK_INTERVAL) {
    float gyroX = mpu.getGyroX();
    float gyroY = mpu.getGyroY();
    float gyroZ = mpu.getGyroZ();
    
    // Если устройство неподвижно (угловые скорости малы)
    if (abs(gyroX) < DRIFT_THRESHOLD && abs(gyroY) < DRIFT_THRESHOLD && abs(gyroZ) < DRIFT_THRESHOLD) {
      // Аккумулируем дрейф по всем осям
      roll_drift_accumulator += gyroX * (STILL_CHECK_INTERVAL / 1000.0);
      pitch_drift_accumulator += gyroY * (STILL_CHECK_INTERVAL / 1000.0);
      yaw_drift_accumulator += gyroZ * (STILL_CHECK_INTERVAL / 1000.0);
      
      // Если накопилось достаточно дрейфа, применяем коррекцию
      if (abs(roll_drift_accumulator) > DRIFT_THRESHOLD) {
        roll_drift_correction -= roll_drift_accumulator * DRIFT_COMPENSATION_RATE;
        roll_drift_accumulator *= 0.9; // Частично сбрасываем аккумулятор
      }
      
      if (abs(pitch_drift_accumulator) > DRIFT_THRESHOLD) {
        pitch_drift_correction -= pitch_drift_accumulator * DRIFT_COMPENSATION_RATE;
        pitch_drift_accumulator *= 0.9;
      }
      
      if (abs(yaw_drift_accumulator) > DRIFT_THRESHOLD) {
        yaw_drift_correction -= yaw_drift_accumulator * DRIFT_COMPENSATION_RATE;
        yaw_drift_accumulator *= 0.9;
      }
    } else {
      // Если устройство движется, сбрасываем аккумуляторы дрейфа
      roll_drift_accumulator *= 0.5;
      pitch_drift_accumulator *= 0.5;
      yaw_drift_accumulator *= 0.5;
    }
    
    last_still_check = current_time;
  }
}

// Инициализация BLE
void initBLE() {
  Serial.println("Инициализация BLE...");
  
  BLEDevice::init("VR-HeadTracker-MPU6050");
  
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
  pCharacteristic->setValue("VR Head Tracker Ready");
  
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  
  BLEDevice::startAdvertising();
  
  Serial.println("BLE сервер запущен!");
  Serial.println("Имя устройства: VR-HeadTracker-MPU6050");
  Serial.println("Service UUID: " + String(SERVICE_UUID));
  Serial.println("Characteristic UUID: " + String(CHARACTERISTIC_UUID));
  Serial.println("Ожидание BLE подключений...");
}

// Обработка команд
String processCommand(String command) {
  command.trim();
  command.toUpperCase();
  
  if (command == "C" || command == "CALIBRATE") {
    if (mpu_initialized) {
      calibrateMPU();
      return "Calibration complete";
    } else {
      return "MPU6050 not connected";
    }
  }
  else if (command == "Z" || command == "ZERO") {
    if (mpu_initialized) {
      setZeroPoint();
      return "Zero point set";
    } else {
      return "MPU6050 not connected";
    }
  }
  else if (command == "R" || command == "RESET") {
    resetZeroPoint();
    return "Zero point reset";
  }
  else if (command == "A" || command == "AUTO") {
    auto_calibration_enabled = !auto_calibration_enabled;
    return "Auto-calibration: " + String(auto_calibration_enabled ? "ON" : "OFF");
  }
  else if (command == "S" || command == "SETTINGS") {
    return getSettingsString();
  }
  else if (command == "D" || command == "DATA") {
    if (mpu_initialized) {
      return getRawDataString();
    } else {
      return "MPU6050 not connected";
    }
  }
  else if (command == "I" || command == "INFO") {
    return "VR Head Tracker v1.0, MPU6050, BLE enabled";
  }
  else if (command == "T" || command == "SCAN") {
    return "I2C scan requested - check serial console";
  }
  else if (command == "B" || command == "BLE") {
    bleEnabled = !bleEnabled;
    if (bleEnabled) {
      initBLE();
      return "BLE enabled";
    } else {
      if (pServer != NULL) {
        BLEDevice::deinit();
      }
      return "BLE disabled";
    }
  }
  else if (command == "H" || command == "HELP") {
    return "Commands: C(calibrate), Z(zero), R(reset), A(auto), S(settings), D(data), I(info), B(ble toggle), H(help), STATUS, ANGLE, Y(reset all drift), X(reset roll drift), P(reset pitch drift), W(reset yaw drift)";
  }
  else if (command == "STATUS") {
    String status = "MPU6050: ";
    status += mpu_initialized ? "Connected" : "Disconnected";
    status += ", BLE: ";
    status += bleDeviceConnected ? "Connected" : "Disconnected";
    status += ", Zero: ";
    status += zero_point_set ? "Set" : "Not set";
    status += ", Roll Drift: ";
    status += String(roll_drift_correction, 3);
    status += ", Pitch Drift: ";
    status += String(pitch_drift_correction, 3);
    status += ", Yaw Drift: ";
    status += String(yaw_drift_correction, 3);
    return status;
  }
  else if (command == "ANGLE") {
    if (mpu_initialized) {
      mpu.update();
      float roll_display = mpu.getAngleX() + roll_drift_correction;
      float pitch_display = mpu.getAngleY() + pitch_drift_correction;
      float yaw_display = mpu.getAngleZ() + yaw_drift_correction;
      
      return "Current angles - Roll: " + String(roll_display, 2) + 
             ", Pitch: " + String(pitch_display, 2) + 
             ", Yaw: " + String(yaw_display, 2);
    } else {
      return "MPU6050 not connected";
    }
  }
  else if (command == "Y" || command == "RESETDRIFT") {
    // Сброс компенсации дрейфа по всем осям
    roll_drift_correction = 0;
    pitch_drift_correction = 0;
    yaw_drift_correction = 0;
    roll_drift_accumulator = 0;
    pitch_drift_accumulator = 0;
    yaw_drift_accumulator = 0;
    return "All axis drift compensation reset";
  }
  else if (command == "X" || command == "RESETROLL") {
    // Сброс компенсации дрейфа Roll
    roll_drift_correction = 0;
    roll_drift_accumulator = 0;
    return "Roll (X) drift compensation reset";
  }
  else if (command == "P" || command == "RESETPITCH") {
    // Сброс компенсации дрейфа Pitch
    pitch_drift_correction = 0;
    pitch_drift_accumulator = 0;
    return "Pitch (Y) drift compensation reset";
  }
  else if (command == "W" || command == "RESETYAW") {
    // Сброс компенсации дрейфа Yaw
    yaw_drift_correction = 0;
    yaw_drift_accumulator = 0;
    return "Yaw (Z) drift compensation reset";
  }
  else {
    return "Unknown command. Send H for help";
  }
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    Serial.print("> ");
    Serial.println(command);
    
    String response = processCommand(command);
    Serial.println(response);
  }
}

String getSettingsString() {
  String settings = "SETTINGS: ";
  settings += "MPU6050=";
  settings += mpu_initialized ? "OK" : "NO";
  settings += ", Calibrated=";
  settings += is_calibrated ? "YES" : "NO";
  settings += ", ZeroPoint=";
  settings += zero_point_set ? "SET" : "NOT SET";
  settings += ", AutoCal=";
  settings += auto_calibration_enabled ? "ON" : "OFF";
  settings += ", BLE=";
  settings += bleEnabled ? "ON" : "OFF";
  settings += ", BLE Connected=";
  settings += bleDeviceConnected ? "YES" : "NO";
  settings += ", Roll Drift=";
  settings += String(roll_drift_correction, 3);
  settings += ", Pitch Drift=";
  settings += String(pitch_drift_correction, 3);
  settings += ", Yaw Drift=";
  settings += String(yaw_drift_correction, 3);
  return settings;
}

String getRawDataString() {
  if (!mpu_initialized) return "MPU6050 not connected";
  
  mpu.update();
  
  String data = "RAW DATA: ";
  data += "Roll=" + String(mpu.getAngleX(), 2);
  data += ", Pitch=" + String(mpu.getAngleY(), 2);
  data += ", Yaw=" + String(mpu.getAngleZ(), 2);
  data += ", RollCorr=" + String(roll_drift_correction, 3);
  data += ", PitchCorr=" + String(pitch_drift_correction, 3);
  data += ", YawCorr=" + String(yaw_drift_correction, 3);
  data += ", GX=" + String(mpu.getGyroX(), 3);
  data += ", GY=" + String(mpu.getGyroY(), 3);
  data += ", GZ=" + String(mpu.getGyroZ(), 3);
  data += ", AX=" + String(mpu.getAccX(), 3);
  data += ", AY=" + String(mpu.getAccY(), 3);
  data += ", AZ=" + String(mpu.getAccZ(), 3);
  data += ", Temp=" + String(mpu.getTemp(), 1);
  return data;
}

void calibrateMPU() {
  if (!mpu_initialized) return;
  
  Serial.println("\n=== ТОЧНАЯ КАЛИБРОВКА MPU6050 ===");
  Serial.println("1. Положите устройство на РОВНУЮ горизонтальную поверхность");
  Serial.println("2. Убедитесь, что оно НЕПОДВИЖНО");
  Serial.println("3. Калибровка займет 3 секунды");
  Serial.println("Начинаем через 3 секунды...");
  
  for (int i = 3; i > 0; i--) {
    Serial.print(i);
    Serial.print("... ");
    delay(1000);
  }
  Serial.println("\nНачинаем калибровку...");
  
  mpu.calcOffsets();
  delay(1000);
  
  saveMPUCalibration();
  is_calibrated = true;
  
  // Сброс компенсации дрейфа после калибровки
  roll_drift_correction = 0;
  pitch_drift_correction = 0;
  yaw_drift_correction = 0;
  roll_drift_accumulator = 0;
  pitch_drift_accumulator = 0;
  yaw_drift_accumulator = 0;
  
  Serial.println("\nКалибровка завершена! Дрейф по всем осям сброшен.");
}

void saveMPUCalibration() {
  calib_data.gyro_offset_x = 0;
  calib_data.gyro_offset_y = 0;
  calib_data.gyro_offset_z = 0;
  calib_data.accel_offset_x = 0;
  calib_data.accel_offset_y = 0;
  calib_data.accel_offset_z = 0;
  calib_data.gyro_drift_threshold = 0.1;
  calib_data.calibration_samples = 1000;
  
  saveCalibrationData();
}

void setZeroPoint() {
  if (!mpu_initialized) return;
  
  mpu.update();
  
  zero_point.roll_zero = mpu.getAngleX() + roll_drift_correction; // Учитываем текущую коррекцию
  zero_point.pitch_zero = mpu.getAngleY() + pitch_drift_correction;
  zero_point.yaw_zero = mpu.getAngleZ() + yaw_drift_correction;
  
  zero_point_set = true;
  saveZeroPoint();
  
  Serial.println("\nНулевая точка установлена!");
  Serial.print("Roll с коррекцией дрейфа: ");
  Serial.println(zero_point.roll_zero, 2);
  Serial.print("Pitch с коррекцией дрейфа: ");
  Serial.println(zero_point.pitch_zero, 2);
  Serial.print("Yaw с коррекцией дрейфа: ");
  Serial.println(zero_point.yaw_zero, 2);
}

void resetZeroPoint() {
  zero_point_set = false;
  preferences.remove("zero_point");
  Serial.println("Нулевая точка сброшена!");
}

void checkAndCorrectDrift() {
  if (!mpu_initialized) return;
  
  float gyroX = mpu.getGyroX();
  float gyroY = mpu.getGyroY();
  float gyroZ = mpu.getGyroZ();
  
  // Дополнительная медленная коррекция если устройство неподвижно
  if (abs(gyroX) < DRIFT_THRESHOLD && abs(gyroY) < DRIFT_THRESHOLD && abs(gyroZ) < DRIFT_THRESHOLD) {
    filtered_roll *= 0.999;
    filtered_pitch *= 0.999;
    filtered_yaw *= 0.999;
  }
}

void loadCalibrationData() {
  calib_data.gyro_offset_x = preferences.getFloat("gyro_x", 0.0);
  calib_data.gyro_offset_y = preferences.getFloat("gyro_y", 0.0);
  calib_data.gyro_offset_z = preferences.getFloat("gyro_z", 0.0);
  
  calib_data.accel_offset_x = preferences.getFloat("accel_x", 0.0);
  calib_data.accel_offset_y = preferences.getFloat("accel_y", 0.0);
  calib_data.accel_offset_z = preferences.getFloat("accel_z", 0.0);
  
  calib_data.gyro_drift_threshold = preferences.getFloat("drift_thresh", 0.1);
  calib_data.calibration_samples = preferences.getInt("calib_samples", 1000);
  
  is_calibrated = preferences.getBool("is_calibrated", false);
}

void saveCalibrationData() {
  preferences.putFloat("gyro_x", calib_data.gyro_offset_x);
  preferences.putFloat("gyro_y", calib_data.gyro_offset_y);
  preferences.putFloat("gyro_z", calib_data.gyro_offset_z);
  
  preferences.putFloat("accel_x", calib_data.accel_offset_x);
  preferences.putFloat("accel_y", calib_data.accel_offset_y);
  preferences.putFloat("accel_z", calib_data.accel_offset_z);
  
  preferences.putFloat("drift_thresh", calib_data.gyro_drift_threshold);
  preferences.putInt("calib_samples", calib_data.calibration_samples);
  
  preferences.putBool("is_calibrated", is_calibrated);
  
  Serial.println("Калибровочные данные сохранены");
}

void loadZeroPoint() {
  zero_point.roll_zero = preferences.getFloat("roll_zero", 0.0);
  zero_point.pitch_zero = preferences.getFloat("pitch_zero", 0.0);
  zero_point.yaw_zero = preferences.getFloat("yaw_zero", 0.0);
  
  zero_point_set = preferences.getBool("zero_set", false);
}

void saveZeroPoint() {
  preferences.putFloat("roll_zero", zero_point.roll_zero);
  preferences.putFloat("pitch_zero", zero_point.pitch_zero);
  preferences.putFloat("yaw_zero", zero_point.yaw_zero);
  
  preferences.putBool("zero_set", zero_point_set);
  
  Serial.println("Нулевая точка сохранена");
}

void scanI2C() {
  Serial.println("\nСканирование I2C шины...");
  byte error, address;
  int nDevices = 0;
  
  for(address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("Устройство найдено по адресу 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.print(" (");
      Serial.print(address);
      Serial.print(")");
      
      if (address == 0x68) {
        Serial.print(" - MPU6050 (AD0 к GND)");
      } else if (address == 0x69) {
        Serial.print(" - MPU6050 (AD0 к 3.3V)");
      }
      
      Serial.println();
      nDevices++;
    }
  }
  
  if (nDevices == 0) {
    Serial.println("I2C устройства не найдены!");
  } else {
    Serial.print("Найдено устройств: ");
    Serial.println(nDevices);
  }
  Serial.println();
}

void setupMPU() {
  // Эта функция оставлена для совместимости, но фактически не используется
  // так как настройки MPU6050 выполняются в библиотеке MPU6050_light
}
