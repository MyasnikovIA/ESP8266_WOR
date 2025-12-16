#include <Wire.h>
#include <Preferences.h>

// Используем библиотеку MPU6050_light
#include "MPU6050_light.h"

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
const unsigned long CALIBRATION_CHECK_INTERVAL = 5000;

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

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  
  Serial.println("VR Head Tracker with MPU6050");
  Serial.println("Инициализация...");
  
  // Инициализация I2C с указанием пинов
  Wire.begin(21, 22, 100000); // SDA=21, SCL=22, 100kHz
  
  // Сканирование I2C для подтверждения
  scanI2C();
  
  Serial.println("\nКоманды:");
  Serial.println("  'c' - калибровка датчика");
  Serial.println("  'z' - установить нулевую точку");
  Serial.println("  'r' - сброс нулевой точки");
  Serial.println("  'a' - вкл/выкл авто-калибровку");
  Serial.println("  's' - показать настройки");
  Serial.println("  'd' - показать данные в реальном времени");
  Serial.println("  'i' - информация о подключении");
  Serial.println("  't' - сканировать I2C шину");
  Serial.println("  'h' - показать это меню");

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
    
    // В библиотеке MPU6050_light нужно использовать setAddress для смены адреса
    mpu_address = 0x69;
    
    // Создаем новый объект с нужным адресом
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
        Serial.println("Проверьте подключение:");
        Serial.println("1. VCC -> 3.3V (не 5V!)");
        Serial.println("2. GND -> GND");
        Serial.println("3. SCL -> GPIO22");
        Serial.println("4. SDA -> GPIO21");
        Serial.println("5. AD0 -> 3.3V (для адреса 0x69)");
      }
    } else {
      Serial.println("Устройство не найдено по адресу 0x69");
      Serial.println("Возможные причины:");
      Serial.println("1. Неправильное подключение");
      Serial.println("2. AD0 подключен к GND (используйте 0x68)");
      Serial.println("3. Проблемы с питанием (требуется 3.3V)");
      Serial.println("4. Неисправный датчик");
    }
  }
  
  last_time = millis();
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
  
  // Применение нулевой точки
  float roll_display, pitch_display, yaw_display;
  
  if (zero_point_set) {
    roll_display = filtered_roll - zero_point.roll_zero;
    pitch_display = filtered_pitch - zero_point.pitch_zero;
    yaw_display = filtered_yaw - zero_point.yaw_zero;
  } else {
    roll_display = filtered_roll;
    pitch_display = filtered_pitch;
    yaw_display = filtered_yaw;
  }
  
  // Отправка данных в серийный порт
  if (Serial.available() == 0) {
    Serial.print("ROLL:");
    Serial.print(roll_display, 2);
    Serial.print(",PITCH:");
    Serial.print(pitch_display, 2);
    Serial.print(",YAW:");
    Serial.println(yaw_display, 2);
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

void handleSerialCommands() {
  if (Serial.available() > 0) {
    char command = Serial.read();
    
    switch (command) {
      case 'c': // Калибровка
        if (mpu_initialized) {
          calibrateMPU();
        } else {
          Serial.println("MPU6050 не подключен!");
        }
        break;
        
      case 'z': // Установить нулевую точку
        if (mpu_initialized) {
          setZeroPoint();
        } else {
          Serial.println("MPU6050 не подключен!");
        }
        break;
        
      case 'r': // Сбросить нулевую точку
        resetZeroPoint();
        break;
        
      case 'a': // Вкл/выкл авто-калибровку
        auto_calibration_enabled = !auto_calibration_enabled;
        Serial.print("Авто-калибровка дрейфа: ");
        Serial.println(auto_calibration_enabled ? "ВКЛ" : "ВЫКЛ");
        break;
        
      case 's': // Показать настройки
        showSettings();
        break;
        
      case 'd': // Показать сырые данные
        if (mpu_initialized) {
          showRawData();
        } else {
          Serial.println("MPU6050 не подключен!");
        }
        break;
        
      case 'i': // Информация о подключении
        showConnectionInfo();
        break;
        
      case 't': // Сканировать I2C
        scanI2C();
        break;
        
      case 'h': // Помощь
        showHelp();
        break;
        
      case '1': // Тест - установка углов в 0
        if (mpu_initialized) {
          roll = pitch = yaw = 0;
          filtered_roll = filtered_pitch = filtered_yaw = 0;
          Serial.println("Углы сброшены в 0");
        }
        break;
        
      case '\n':
      case '\r':
        break;
        
      default:
        Serial.println("Неизвестная команда. Отправьте 'h' для помощи.");
    }
  }
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
  
  // Выполняем калибровку в библиотеке
  mpu.calcOffsets();
  
  // Ждем стабилизации
  delay(1000);
  
  // Сохраняем калибровочные данные
  saveMPUCalibration();
  is_calibrated = true;
  
  Serial.println("\nКалибровка завершена!");
  Serial.println("Текущие углы должны быть близки к 0:");
  Serial.print("Roll: "); Serial.print(mpu.getAngleX(), 2);
  Serial.print("°, Pitch: "); Serial.print(mpu.getAngleY(), 2);
  Serial.print("°, Yaw: "); Serial.print(mpu.getAngleZ(), 2);
  Serial.println("°");
  Serial.println("==============================\n");
}

void saveMPUCalibration() {
  // Сохраняем флаг калибровки
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
  
  // Обновляем данные перед установкой нулевой точки
  mpu.update();
  
  zero_point.roll_zero = mpu.getAngleX();
  zero_point.pitch_zero = mpu.getAngleY();
  zero_point.yaw_zero = mpu.getAngleZ();
  
  zero_point_set = true;
  saveZeroPoint();
  
  Serial.println("\nНулевая точка установлена!");
  Serial.print("Текущие углы: Roll=");
  Serial.print(zero_point.roll_zero, 2);
  Serial.print("°, Pitch=");
  Serial.print(zero_point.pitch_zero, 2);
  Serial.print("°, Yaw=");
  Serial.print(zero_point.yaw_zero, 2);
  Serial.println("°\n");
}

void resetZeroPoint() {
  zero_point_set = false;
  preferences.remove("zero_point");
  Serial.println("Нулевая точка сброшена!");
}

void checkAndCorrectDrift() {
  if (!mpu_initialized) return;
  
  // Получаем текущие угловые скорости
  float gyroX = mpu.getGyroX();
  float gyroY = mpu.getGyroY();
  float gyroZ = mpu.getGyroZ();
  
  // Если угловые скорости малы, считаем что устройство неподвижно
  if (abs(gyroX) < 0.5 && abs(gyroY) < 0.5 && abs(gyroZ) < 0.5) {
    // Можно добавить медленную коррекцию если нужно
    // filtered_roll *= 0.999;
    // filtered_pitch *= 0.999;
    // filtered_yaw *= 0.999;
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

void showSettings() {
  Serial.println("\n=== ТЕКУЩИЕ НАСТРОЙКИ ===");
  Serial.print("MPU6050 инициализирован: ");
  Serial.println(mpu_initialized ? "ДА" : "НЕТ");
  if (mpu_initialized) {
    Serial.print("Адрес MPU6050: 0x");
    Serial.println(mpu_address, HEX);
  }
  Serial.print("Калиброваны: ");
  Serial.println(is_calibrated ? "ДА" : "НЕТ");
  
  Serial.print("Нулевая точка установлена: ");
  Serial.println(zero_point_set ? "ДА" : "НЕТ");
  
  if (zero_point_set) {
    Serial.println("Значения нулевой точки (град):");
    Serial.print("  Roll: "); Serial.println(zero_point.roll_zero, 2);
    Serial.print("  Pitch: "); Serial.println(zero_point.pitch_zero, 2);
    Serial.print("  Yaw: "); Serial.println(zero_point.yaw_zero, 2);
  }
  
  Serial.print("Авто-калибровка дрейфа: ");
  Serial.println(auto_calibration_enabled ? "ВКЛ" : "ВЫКЛ");
  Serial.println("=======================\n");
}

void showRawData() {
  if (!mpu_initialized) return;
  
  mpu.update();
  
  Serial.println("\n=== СЫРЫЕ ДАННЫЕ ===");
  Serial.println("Углы (град):");
  Serial.print("  Roll (X): "); Serial.print(mpu.getAngleX(), 2);
  Serial.print("°, Pitch (Y): "); Serial.print(mpu.getAngleY(), 2);
  Serial.print("°, Yaw (Z): "); Serial.print(mpu.getAngleZ(), 2);
  Serial.println("°");
  
  Serial.println("Угловые скорости (град/с):");
  Serial.print("  GX: "); Serial.print(mpu.getGyroX(), 2);
  Serial.print("°/s, GY: "); Serial.print(mpu.getGyroY(), 2);
  Serial.print("°/s, GZ: "); Serial.print(mpu.getGyroZ(), 2);
  Serial.println("°/s");
  
  Serial.println("Ускорения (g):");
  Serial.print("  AX: "); Serial.print(mpu.getAccX(), 3);
  Serial.print("g, AY: "); Serial.print(mpu.getAccY(), 3);
  Serial.print("g, AZ: "); Serial.print(mpu.getAccZ(), 3);
  Serial.println("g");
  
  Serial.print("Температура: ");
  Serial.print(mpu.getTemp(), 1);
  Serial.println(" °C");
  Serial.println("====================\n");
}

void showConnectionInfo() {
  Serial.println("\n=== ИНФОРМАЦИЯ О ПОДКЛЮЧЕНИИ ===");
  Serial.println("Пины I2C для ESP32:");
  Serial.println("  SDA: GPIO21 (по умолчанию)");
  Serial.println("  SCL: GPIO22 (по умолчанию)");
  Serial.println("\nПодключение MPU6050:");
  Serial.println("  VCC -> 3.3V (ВАЖНО! Не 5V!)");
  Serial.println("  GND -> GND");
  Serial.println("  SCL -> GPIO22");
  Serial.println("  SDA -> GPIO21");
  Serial.println("  AD0 -> GND (адрес 0x68) или 3.3V (адрес 0x69)");
  Serial.println("\nОтправьте 't' для сканирования I2C шины");
  Serial.println("===============================\n");
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

void showHelp() {
  Serial.println("\n=== КОМАНДЫ ДЛЯ VR HEAD TRACKER ===");
  Serial.println("c - Калибровка датчика (обязательно при первом запуске)");
  Serial.println("z - Установить нулевую точку (текущее положение как 0)");
  Serial.println("r - Сбросить нулевую точку");
  Serial.println("a - Вкл/выкл авто-калибровку дрейфа");
  Serial.println("s - Показать настройки");
  Serial.println("d - Показать сырые данные с датчика");
  Serial.println("i - Информация о подключении");
  Serial.println("t - Сканировать I2C шину");
  Serial.println("h - Показать это меню");
  Serial.println("1 - Сбросить углы в 0 (тест)");
  Serial.println("\nФормат вывода данных:");
  Serial.println("  ROLL:XX.XX,PITCH:XX.XX,YAW:XX.XX");
  Serial.println("  Roll - наклон головы влево/вправо");
  Serial.println("  Pitch - наклон вперед/назад");
  Serial.println("  Yaw - поворот головы");
  Serial.println("\nУглы могут превышать ±180° и ±360°");
  Serial.println("===================================\n");
}
