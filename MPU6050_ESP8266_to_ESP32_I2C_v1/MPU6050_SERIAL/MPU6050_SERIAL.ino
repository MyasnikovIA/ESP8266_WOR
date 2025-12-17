#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
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

// Углы космического корабля (без ограничений)
float spacecraft_pitch = 0;
float spacecraft_roll = 0;
float spacecraft_yaw = 0;

// Последнее время измерения
unsigned long last_time = 0;

// Текущий адрес I2C
uint8_t current_i2c_address = 0x68;

// Командный режим
bool commandMode = false; // Изменено: по умолчанию не командный режим
String inputString = "";

// Флаг автоматического запуска
bool autoStartCompleted = false;

// Прототипы функций
bool scanI2CForMPU();
bool loadCalibrationData();
void saveCalibrationData();
void calibrateGyroAccel();
void setZeroOrientation();
void printHelp();
void processCommand(String cmd);
void updateOrientation();
void performAutoStartSequence();

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  
  Serial.println("\n========================================");
  Serial.println("Система ориентации космического корабля");
  Serial.println("========================================");
  
  // Инициализация EEPROM
  EEPROM.begin(sizeof(CalibrationData));
  
  // Поиск MPU6050 на шине I2C
  Serial.println("Сканирование шины I2C...");
  
  if (!scanI2CForMPU()) {
    Serial.println("MPU6050 не найден на шине I2C!");
    Serial.println("Проверьте подключение и перезагрузите систему.");
    while (1) {
      delay(1000);
    }
  }
  
  // ВЫПОЛНЕНИЕ АВТОМАТИЧЕСКОЙ ПОСЛЕДОВАТЕЛЬНОСТИ ЗАПУСКА
  performAutoStartSequence();
  
  // Инициализация времени
  last_time = micros();
  
  // Вывод справки (после автоматического запуска)
  Serial.println("\nАвтоматический запуск завершен. Система готова.");
  Serial.println("Для доступа к командам отправьте 'c' в монитор порта");
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
        Serial.println("\nПереход в командный режим");
        printHelp();
      }
    }
  }
}

// Автоматическая последовательность запуска
void performAutoStartSequence() {
  Serial.println("\n=== АВТОМАТИЧЕСКИЙ ЗАПУСК ===");
  
  // Шаг 1: Попытка загрузить сохраненные калибровочные данные
  Serial.println("\n1. Попытка загрузки калибровочных данных...");
  if (loadCalibrationData()) {
    Serial.println("   ✓ Калибровочные данные загружены из памяти");
    
    // Проверка, совпадает ли адрес I2C
    if (calibData.i2c_address != current_i2c_address) {
      Serial.print("   ⚠ Адрес I2C изменился: было 0x");
      Serial.print(calibData.i2c_address, HEX);
      Serial.print(", стало 0x");
      Serial.println(current_i2c_address, HEX);
      Serial.println("   Требуется повторная калибровка");
      goto NEED_CALIBRATION;
    }
    
  } else {
    Serial.println("   ✗ Калибровочные данные не найдены");
    goto NEED_CALIBRATION;
  }
  
  // Проверка валидности данных
  Serial.println("\n2. Проверка валидности данных...");
  if (calibData.validMarker[0] != 'M' || calibData.validMarker[1] != 'P' || 
      calibData.validMarker[2] != 'U' || calibData.validMarker[3] != '6') {
    Serial.println("   ✗ Маркер данных неверен");
    goto NEED_CALIBRATION;
  }
  
  // Проверка разумности значений
  if (abs(calibData.gyroX_offset) > 0.5 || abs(calibData.gyroY_offset) > 0.5 || 
      abs(calibData.gyroZ_offset) > 0.5) {
    Serial.println("   ✗ Значения смещений гироскопа выходят за допустимые пределы");
    goto NEED_CALIBRATION;
  }
  
  Serial.println("   ✓ Данные валидны");
  goto START_STREAMING;
  
NEED_CALIBRATION:
  // Шаг 3: Калибровка
  Serial.println("\n3. Выполнение калибровки...");
  calibrateGyroAccel();
  
  // Шаг 4: Установка нулевой ориентации
  Serial.println("\n4. Установка нулевой ориентации...");
  setZeroOrientation();
  
  // Шаг 5: Сохранение данных
  Serial.println("\n5. Сохранение калибровочных данных...");
  saveCalibrationData();
  
  // Шаг 6: Повторная загрузка для проверки
  Serial.println("\n6. Проверка сохраненных данных...");
  if (loadCalibrationData()) {
    Serial.println("   ✓ Данные успешно сохранены и загружены");
  } else {
    Serial.println("   ✗ Ошибка при загрузке сохраненных данных");
  }
  
START_STREAMING:
  // Шаг 7: Запуск потоковых данных
  Serial.println("\n7. Запуск потоковых данных ориентации...");
  commandMode = false;
  autoStartCompleted = true;
  last_time = micros(); // Сброс времени для точных измерений
  
  Serial.println("\n=== АВТОМАТИЧЕСКИЙ ЗАПУСК ЗАВЕРШЕН ===");
}

// Сканирование шины I2C для поиска MPU6050
bool scanI2CForMPU() {
  Wire.begin();
  
  Serial.println("Начинаем сканирование I2C шины...");
  
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("   Найден I2C-устройство на адресе 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      
      // Попытка инициализации как MPU6050
      if (mpu.begin(address)) {
        Serial.println(" - обнаружен MPU6050!");
        current_i2c_address = address;
        calibData.i2c_address = address;
        
        // Настройка параметров датчика
        mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
        mpu.setGyroRange(MPU6050_RANGE_500_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
        
        delay(100);
        return true;
      } else {
        Serial.println(" - не MPU6050");
      }
    }
  }
  
  Serial.println("Сканирование завершено");
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
  Serial.println("   ✓ Калибровочные данные сохранены в энергонезависимую память");
}

// Калибровка гироскопа и акселерометра
void calibrateGyroAccel() {
  Serial.println("   Подготовка к калибровке...");
  Serial.println("   Установите корабль в неподвижное положение");
  
  for (int i = 3; i > 0; i--) {
    Serial.print("   ");
    Serial.println(i);
    delay(1000);
  }
  
  Serial.println("   Начинаем калибровку... Не двигайте корабль!");
  
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
  
  Serial.println("\n   ✓ Калибровка завершена!");
  Serial.print("   Смещения гироскопа: ");
  Serial.print("X="); Serial.print(calibData.gyroX_offset, 6);
  Serial.print(" Y="); Serial.print(calibData.gyroY_offset, 6);
  Serial.print(" Z="); Serial.println(calibData.gyroZ_offset, 6);
  
  Serial.print("   Смещения акселерометра: ");
  Serial.print("X="); Serial.print(calibData.accelX_offset, 6);
  Serial.print(" Y="); Serial.print(calibData.accelY_offset, 6);
  Serial.print(" Z="); Serial.println(calibData.accelZ_offset, 6);
}

// Установка текущей ориентации как нулевой
void setZeroOrientation() {
  Serial.println("   Фиксация текущей ориентации как нулевой точки...");
  
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
  
  // Сбрасываем текущие углы космического корабля
  spacecraft_pitch = 0;
  spacecraft_roll = 0;
  spacecraft_yaw = 0;
  
  Serial.print("   ✓ Нулевая точка установлена: ");
  Serial.print("Pitch="); Serial.print(calibData.zero_pitch, 2);
  Serial.print("° Roll="); Serial.print(calibData.zero_roll, 2);
  Serial.println("° Yaw=0°");
}

// Обновление ориентации космического корабля
void updateOrientation() {
  // Получаем данные с датчика
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  // Компенсируем смещения
  float gyroX = g.gyro.x - calibData.gyroX_offset;
  float gyroY = g.gyro.y - calibData.gyroY_offset;
  float gyroZ = g.gyro.z - calibData.gyroZ_offset;
  
  float ax = a.acceleration.x - calibData.accelX_offset;
  float ay = a.acceleration.y - calibData.accelY_offset;
  float az = a.acceleration.z - calibData.accelZ_offset;
  
  // Рассчитываем время
  unsigned long current_time = micros();
  float dt = (current_time - last_time) / 1000000.0;
  last_time = current_time;
  
  // Преобразование в градусы/секунду
  float gyro_pitch_rate = gyroY * (180.0 / PI);
  float gyro_roll_rate = gyroX * (180.0 / PI);
  float gyro_yaw_rate = gyroZ * (180.0 / PI);
  
  // Интегрируем угловые скорости для получения углов
  spacecraft_pitch += gyro_pitch_rate * dt;
  spacecraft_roll += gyro_roll_rate * dt;
  spacecraft_yaw += gyro_yaw_rate * dt;
  
  // Для Pitch и Roll используем комплементарный фильтр с акселерометром
  float accel_pitch = atan2(ay, az) * 180 / PI - calibData.zero_pitch;
  float accel_roll = atan2(-ax, sqrt(ay * ay + az * az)) * 180 / PI - calibData.zero_roll;
  
  float alpha = 0.98; // Больший вес гироскопу (корабль в космосе)
  spacecraft_pitch = alpha * spacecraft_pitch + (1 - alpha) * accel_pitch;
  spacecraft_roll = alpha * spacecraft_roll + (1 - alpha) * accel_roll;
  
  // Вывод информации об ориентации в компактном формате
  Serial.print("Pitch:");
  Serial.print(spacecraft_pitch, 2);
  Serial.print(" Roll:");
  Serial.print(spacecraft_roll, 2);
  Serial.print(" Yaw:");
  Serial.print(spacecraft_yaw, 2);
  Serial.println();
  
  delay(50); // Частота обновления 20 Гц
}

// Вывод справки по командам
void printHelp() {
  Serial.println("\n=== КОМАНДЫ УПРАВЛЕНИЯ ===");
  Serial.println("calib   - Калибровка датчиков (гироскоп + акселерометр)");
  Serial.println("zero    - Установить текущую ориентацию как нулевую");
  Serial.println("start   - Начать вывод ориентации в реальном времени");
  Serial.println("stop    - Остановить вывод ориентации");
  Serial.println("save    - Сохранить текущие настройки");
  Serial.println("load    - Загрузить настройки из памяти");
  Serial.println("status  - Показать текущие настройки");
  Serial.println("help    - Показать эту справку");
  Serial.println("auto    - Выполнить автоматическую последовательность запуска");
  Serial.println("exit    - Выйти из командного режима (начать потоковые данные)");
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
    Serial.println("\nЗапуск мониторинга ориентации...");
    Serial.println("Для выхода в командный режим отправьте 'c'");
    last_time = micros(); // Сброс времени
  } else if (cmd == "stop") {
    commandMode = true;
    Serial.println("\nМониторинг ориентации остановлен");
  } else if (cmd == "save") {
    saveCalibrationData();
  } else if (cmd == "load") {
    if (loadCalibrationData()) {
      Serial.println("Настройки загружены из памяти");
    } else {
      Serial.println("Ошибка загрузки настроек");
    }
  } else if (cmd == "status") {
    Serial.println("\n=== ТЕКУЩИЕ НАСТРОЙКИ ===");
    Serial.print("Адрес I2C: 0x");
    Serial.println(calibData.i2c_address, HEX);
    Serial.print("Нулевая точка - Pitch: ");
    Serial.print(calibData.zero_pitch, 2);
    Serial.print("°, Roll: ");
    Serial.print(calibData.zero_roll, 2);
    Serial.print("°, Yaw: ");
    Serial.println(calibData.zero_yaw, 2);
    Serial.print("Смещения гироскопа - X: ");
    Serial.print(calibData.gyroX_offset, 6);
    Serial.print(", Y: ");
    Serial.print(calibData.gyroY_offset, 6);
    Serial.print(", Z: ");
    Serial.println(calibData.gyroZ_offset, 6);
  } else if (cmd == "help") {
    printHelp();
  } else if (cmd == "auto") {
    Serial.println("\nВыполнение автоматической последовательности запуска...");
    performAutoStartSequence();
  } else if (cmd == "exit") {
    commandMode = false;
    Serial.println("\nВыход в режим потоковых данных...");
    last_time = micros();
  } else if (cmd == "") {
    // Пустая команда - ничего не делаем
  } else {
    Serial.print("Неизвестная команда: ");
    Serial.println(cmd);
    Serial.println("Введите 'help' для списка команд");
  }
}
