#include <Wire.h>
#include <math.h>

// Адрес датчика QMC5883L
#define QMC5883L_ADDR 0x0D

// Регистры датчика
#define QMC5883L_REG_DATA 0x00
#define QMC5883L_REG_STATUS 0x06
#define QMC5883L_REG_CONTROL1 0x09
#define QMC5883L_REG_CONTROL2 0x0A

// Структура для хранения данных
struct SensorData {
  int16_t x, y, z;
  float roll, pitch, heading;
  int azimuth;
  String direction;
};

// Глобальные переменные
SensorData data;
unsigned long lastPrintTime = 0;
const unsigned long printInterval = 500; // Интервал вывода в мс

// Калибровочные значения (замените на свои после калибровки)
int calMinX = -1286, calMaxX = 1532;
int calMinY = -1395, calMaxY = 1156;
int calMinZ = -1298, calMaxZ = 1427;

// Настройки
String outputMode = "full"; // "full", "compact", "angles", "raw"
bool autoOutput = true;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // Инициализация датчика
  initSensor();
  
  Serial.println("{\"status\":\"ready\",\"device\":\"GY-271\",\"mode\":\"json\",\"interval_ms\":500}");
}

void loop() {
  // Обработка команд
  handleCommands();
  
  // Чтение данных с датчика
  if (readSensor()) {
    // Расчет углов
    calculateAngles();
    
    // Вывод в формате JSON по интервалу
    if (autoOutput && millis() - lastPrintTime >= printInterval) {
      printJSON();
      lastPrintTime = millis();
    }
  }
  
  delay(10);
}

// Инициализация датчика
void initSensor() {
  Wire.beginTransmission(QMC5883L_ADDR);
  Wire.write(QMC5883L_REG_CONTROL1);
  Wire.write(0x1D); // Режим: непрерывное измерение, 200Hz, 8Гаусс, 512 отсчетов
  Wire.endTransmission();
  
  Wire.beginTransmission(QMC5883L_ADDR);
  Wire.write(QMC5883L_REG_CONTROL2);
  Wire.write(0x80); // Сброс
  Wire.endTransmission();
  
  delay(10);
}

// Чтение данных с датчика
bool readSensor() {
  Wire.beginTransmission(QMC5883L_ADDR);
  Wire.write(QMC5883L_REG_DATA);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  
  Wire.requestFrom(QMC5883L_ADDR, 6);
  if (Wire.available() < 6) {
    return false;
  }
  
  // Чтение данных (X, Y, Z) - порядок байт: LSB, MSB
  uint8_t x_low = Wire.read();
  uint8_t x_high = Wire.read();
  uint8_t y_low = Wire.read();
  uint8_t y_high = Wire.read();
  uint8_t z_low = Wire.read();
  uint8_t z_high = Wire.read();
  
  data.x = (int16_t)(x_high << 8 | x_low);
  data.y = (int16_t)(y_high << 8 | y_low);
  data.z = (int16_t)(z_high << 8 | z_low);
  
  return true;
}

// Расчет углов в градусах
void calculateAngles() {
  // Применение калибровки
  int calX = map(data.x, calMinX, calMaxX, -1000, 1000);
  int calY = map(data.y, calMinY, calMaxY, -1000, 1000);
  int calZ = map(data.z, calMinZ, calMaxZ, -1000, 1000);
  
  // Расчет крена (Roll) - вращение вокруг оси X
  if (calZ != 0) {
    data.roll = atan2((float)calY, (float)calZ) * 180.0 / M_PI;
  } else {
    data.roll = 0;
  }
  
  // Расчет тангажа (Pitch) - вращение вокруг оси Y
  float denominator = sqrt((float)calY * calY + (float)calZ * calZ);
  if (denominator != 0) {
    data.pitch = atan2((float)-calX, denominator) * 180.0 / M_PI;
  } else {
    data.pitch = 0;
  }
  
  // Расчет курса (Heading) - вращение вокруг оси Z
  if (calX != 0 || calY != 0) {
    data.heading = atan2((float)calY, (float)calX) * 180.0 / M_PI;
    if (data.heading < 0) {
      data.heading += 360.0;
    }
  } else {
    data.heading = 0;
  }
  
  // Расчет азимута (0-359 градусов)
  data.azimuth = (int)data.heading;
  if (data.azimuth >= 360) data.azimuth -= 360;
  if (data.azimuth < 0) data.azimuth += 360;
  
  // Определение направления
  data.direction = getDirection(data.azimuth);
}

// Определение направления по азимуту
String getDirection(int azimuth) {
  if (azimuth >= 337.5 || azimuth < 22.5) return "N";
  if (azimuth >= 22.5 && azimuth < 67.5) return "NE";
  if (azimuth >= 67.5 && azimuth < 112.5) return "E";
  if (azimuth >= 112.5 && azimuth < 157.5) return "SE";
  if (azimuth >= 157.5 && azimuth < 202.5) return "S";
  if (azimuth >= 202.5 && azimuth < 247.5) return "SW";
  if (azimuth >= 247.5 && azimuth < 292.5) return "W";
  if (azimuth >= 292.5 && azimuth < 337.5) return "NW";
  
  return "?";
}

// Вывод данных в формате JSON
void printJSON() {
  if (outputMode == "full") {
    Serial.print("{");
    Serial.print("\"raw\":{\"x\":");
    Serial.print(data.x);
    Serial.print(",\"y\":");
    Serial.print(data.y);
    Serial.print(",\"z\":");
    Serial.print(data.z);
    Serial.print("},");
    
    Serial.print("\"angles\":{\"roll\":");
    Serial.print(data.roll, 2);
    Serial.print(",\"pitch\":");
    Serial.print(data.pitch, 2);
    Serial.print(",\"heading\":");
    Serial.print(data.heading, 2);
    Serial.print("},");
    
    Serial.print("\"compass\":{\"azimuth\":");
    Serial.print(data.azimuth);
    Serial.print(",\"direction\":\"");
    Serial.print(data.direction);
    Serial.print("\"},");
    
    Serial.print("\"timestamp\":");
    Serial.print(millis());
    Serial.println("}");
    
  } else if (outputMode == "compact") {
    Serial.print("{\"r\":");
    Serial.print(data.roll, 1);
    Serial.print(",\"p\":");
    Serial.print(data.pitch, 1);
    Serial.print(",\"h\":");
    Serial.print(data.heading, 1);
    Serial.print(",\"a\":");
    Serial.print(data.azimuth);
    Serial.print(",\"d\":\"");
    Serial.print(data.direction);
    Serial.println("\"}");
    
  } else if (outputMode == "angles") {
    Serial.print("{\"roll\":");
    Serial.print(data.roll, 1);
    Serial.print(",\"pitch\":");
    Serial.print(data.pitch, 1);
    Serial.print(",\"heading\":");
    Serial.print(data.heading, 1);
    Serial.println("}");
    
  } else if (outputMode == "raw") {
    Serial.print("{\"x\":");
    Serial.print(data.x);
    Serial.print(",\"y\":");
    Serial.print(data.y);
    Serial.print(",\"z\":");
    Serial.print(data.z);
    Serial.println("}");
  }
}

// Обработка команд из Serial
void handleCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "mode:full") {
      outputMode = "full";
      Serial.println("{\"response\":\"mode_set\",\"value\":\"full\"}");
    } else if (command == "mode:compact") {
      outputMode = "compact";
      Serial.println("{\"response\":\"mode_set\",\"value\":\"compact\"}");
    } else if (command == "mode:angles") {
      outputMode = "angles";
      Serial.println("{\"response\":\"mode_set\",\"value\":\"angles\"}");
    } else if (command == "mode:raw") {
      outputMode = "raw";
      Serial.println("{\"response\":\"mode_set\",\"value\":\"raw\"}");
    } else if (command == "calibrate") {
      calibrate();
    } else if (command == "auto:on") {
      autoOutput = true;
      Serial.println("{\"response\":\"auto_output\",\"value\":\"on\"}");
    } else if (command == "auto:off") {
      autoOutput = false;
      Serial.println("{\"response\":\"auto_output\",\"value\":\"off\"}");
    } else if (command == "get") {
      if (readSensor()) {
        calculateAngles();
        printJSON();
      }
    } else if (command.startsWith("calibration:")) {
      // Установка калибровочных значений: calibration:minX,maxX,minY,maxY,minZ,maxZ
      parseCalibration(command.substring(12));
    } else if (command == "status") {
      Serial.println("{\"status\":\"running\",\"mode\":\"" + outputMode + "\",\"auto_output\":" + String(autoOutput ? "true" : "false") + ",\"calibration\":{\"x\":[" + String(calMinX) + "," + String(calMaxX) + "],\"y\":[" + String(calMinY) + "," + String(calMaxY) + "],\"z\":[" + String(calMinZ) + "," + String(calMaxZ) + "]}}");
    } else if (command == "help") {
      Serial.println("{\"commands\":[\"mode:full\",\"mode:compact\",\"mode:angles\",\"mode:raw\",\"calibrate\",\"auto:on\",\"auto:off\",\"get\",\"calibration:minX,maxX,minY,maxY,minZ,maxZ\",\"status\",\"help\"]}");
    } else {
      Serial.println("{\"error\":\"unknown_command\",\"received\":\"" + command + "\"}");
    }
  }
}

// Парсинг калибровочных значений из строки
void parseCalibration(String calStr) {
  int values[6];
  int index = 0;
  int lastComma = -1;
  
  for (int i = 0; i <= calStr.length(); i++) {
    if (i == calStr.length() || calStr.charAt(i) == ',') {
      String numStr = calStr.substring(lastComma + 1, i);
      values[index] = numStr.toInt();
      index++;
      lastComma = i;
      if (index >= 6) break;
    }
  }
  
  if (index == 6) {
    calMinX = values[0];
    calMaxX = values[1];
    calMinY = values[2];
    calMaxY = values[3];
    calMinZ = values[4];
    calMaxZ = values[5];
    
    Serial.print("{\"calibration_set\":{\"x\":[");
    Serial.print(calMinX);
    Serial.print(",");
    Serial.print(calMaxX);
    Serial.print("],\"y\":[");
    Serial.print(calMinY);
    Serial.print(",");
    Serial.print(calMaxY);
    Serial.print("],\"z\":[");
    Serial.print(calMinZ);
    Serial.print(",");
    Serial.print(calMaxZ);
    Serial.println("]}}");
  } else {
    Serial.println("{\"error\":\"invalid_calibration_format\",\"expected\":\"minX,maxX,minY,maxY,minZ,maxZ\"}");
  }
}

// Функция для калибровки
void calibrate() {
  Serial.println("{\"calibration\":\"start\",\"message\":\"Вращайте датчик 30 секунд\",\"duration\":30}");
  
  int16_t minX = 32767;
  int16_t maxX = -32768;
  int16_t minY = 32767;
  int16_t maxY = -32768;
  int16_t minZ = 32767;
  int16_t maxZ = -32768;
  
  unsigned long startTime = millis();
  unsigned long lastUpdateTime = startTime;
  
  while (millis() - startTime < 200) { // Калибровка 30 секунд
    if (readSensor()) {
      // Используем явное сравнение вместо min/max для избежания ошибок типов
      if (data.x < minX) minX = data.x;
      if (data.x > maxX) maxX = data.x;
      if (data.y < minY) minY = data.y;
      if (data.y > maxY) maxY = data.y;
      if (data.z < minZ) minZ = data.z;
      if (data.z > maxZ) maxZ = data.z;
      
      // Периодический вывод прогресса
      if (millis() - lastUpdateTime >= 200) {
        Serial.print("{\"calibration_progress\":{\"time_elapsed\":");
        Serial.print((millis() - startTime) / 1000);
        Serial.print(",\"x\":[");
        Serial.print(minX);
        Serial.print(",");
        Serial.print(maxX);
        Serial.print("],\"y\":[");
        Serial.print(minY);
        Serial.print(",");
        Serial.print(maxY);
        Serial.print("],\"z\":[");
        Serial.print(minZ);
        Serial.print(",");
        Serial.print(maxZ);
        Serial.println("]}}");
        lastUpdateTime = millis();
      }
    }
    delay(10);
  }
  
  // Сохранение калибровочных значений
  calMinX = minX;
  calMaxX = maxX;
  calMinY = minY;
  calMaxY = maxY;
  calMinZ = minZ;
  calMaxZ = maxZ;
  
  // Вывод результатов калибровки
  Serial.print("{\"calibration\":\"complete\",\"values\":{\"x\":[");
  Serial.print(calMinX);
  Serial.print(",");
  Serial.print(calMaxX);
  Serial.print("],\"y\":[");
  Serial.print(calMinY);
  Serial.print(",");
  Serial.print(calMaxY);
  Serial.print("],\"z\":[");
  Serial.print(calMinZ);
  Serial.print(",");
  Serial.print(calMaxZ);
  Serial.println("]}}");
  
  // Вывод кода для вставки в программу
  Serial.print("{\"calibration_code\":\"int calMinX = ");
  Serial.print(calMinX);
  Serial.print(", calMaxX = ");
  Serial.print(calMaxX);
  Serial.print("; int calMinY = ");
  Serial.print(calMinY);
  Serial.print(", calMaxY = ");
  Serial.print(calMaxY);
  Serial.print("; int calMinZ = ");
  Serial.print(calMinZ);
  Serial.print(", calMaxZ = ");
  Serial.print(calMaxZ);
  Serial.println(";\"}");
}

// Вспомогательные функции для сравнения (если нужны)
int16_t myMin(int16_t a, int16_t b) {
  return (a < b) ? a : b;
}

int16_t myMax(int16_t a, int16_t b) {
  return (a > b) ? a : b;
}
