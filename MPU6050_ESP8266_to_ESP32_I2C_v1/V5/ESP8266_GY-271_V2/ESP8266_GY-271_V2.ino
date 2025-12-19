#include <QMC5883LCompass.h>

QMC5883LCompass compass;

// Настраиваемые пороги (можно менять через Serial команды)
float angleThreshold = 1.0;     // Порог изменения углов (градусы)
int azimuthThreshold = 2;       // Порог изменения азимута (градусы)
int rawThreshold = 50;          // Порог изменения сырых значений

// Переменные состояния
float prevPitch = 0, prevRoll = 0, prevTilt = 0;
int prevAzimuth = 0, prevX = 0, prevY = 0, prevZ = 0;
unsigned long lastChangeTime = 0;
const unsigned long MIN_CHANGE_INTERVAL = 50; // мс
unsigned int readingCount = 0;
bool firstReading = true;
unsigned long totalReadings = 0, changesDetected = 0;

// Магнитное склонение (декланация) для коррекции компаса
// Для Москвы: +10.5° (восточное склонение)
const float MAGNETIC_DECLINATION = 10.5; // градусы

// Прототипы функций
float calculatePitch(int x, int y, int z);
float calculateRoll(int x, int y, int z);
float calculateTilt(int x, int y, int z);
float calculateHeading(int x, int y);
float calculateTiltCompensatedHeading(int x, int y, int z, float pitch, float roll);
String getDirection(int azimuth);
void printAllAngles(int x, int y, int z);
void sendChangeData(unsigned long timestamp, unsigned int count,
                    int x, int y, int z,
                    float pitch, float roll, float tilt,
                    int azimuth);
void updatePreviousValues(int x, int y, int z,
                         float pitch, float roll, float tilt,
                         int azimuth);
void handleCommand();
void sendStatistics(unsigned long timestamp);

void setup() {
  Serial.begin(115200);
  
  // Инициализация датчика
  compass.init();
  compass.setSmoothing(5, true); // Сглаживание 5 отсчетов
  
  // Ожидание стабилизации Serial порта
  delay(1000);
  
  // Инициализация первых значений
  compass.read();
  prevX = compass.getX();
  prevY = compass.getY();
  prevZ = compass.getZ();
  prevAzimuth = compass.getAzimuth();
  prevPitch = calculatePitch(prevX, prevY, prevZ);
  prevRoll = calculateRoll(prevX, prevY, prevZ);
  prevTilt = calculateTilt(prevX, prevY, prevZ);
  
  Serial.println("{\"event\":\"init\",\"sensor\":\"GY-271 QMC5883L\",\"mode\":\"change_detection\",\"baud\":115200}");
  Serial.println("{\"message\":\"Data sent only when position changes\",\"magnetic_declination\":10.5}");
  Serial.println("{\"commands\":\"status, single, reset_stats, thresholds, help\"}");
}

void loop() {
  totalReadings++;
  
  // Чтение данных с датчика
  compass.read();
  
  // Получение текущих значений
  int currentX = compass.getX();
  int currentY = compass.getY();
  int currentZ = compass.getZ();
  int currentAzimuth = compass.getAzimuth();
  
  // Расчет текущих углов
  float currentPitch = calculatePitch(currentX, currentY, currentZ);
  float currentRoll = calculateRoll(currentX, currentY, currentZ);
  float currentTilt = calculateTilt(currentX, currentY, currentZ);
  
  // Проверка изменений с текущими порогами
  bool positionChanged = false;
  
  if (abs(currentPitch - prevPitch) >= angleThreshold ||
      abs(currentRoll - prevRoll) >= angleThreshold ||
      abs(currentTilt - prevTilt) >= angleThreshold) {
    positionChanged = true;
  }
  
  if (abs(currentAzimuth - prevAzimuth) >= azimuthThreshold) {
    positionChanged = true;
  }
  
  if (abs(currentX - prevX) >= rawThreshold ||
      abs(currentY - prevY) >= rawThreshold ||
      abs(currentZ - prevZ) >= rawThreshold) {
    positionChanged = true;
  }
  
  unsigned long currentTime = millis();
  
  // Если положение изменилось и прошло достаточно времени
  if (positionChanged && (firstReading || (currentTime - lastChangeTime >= MIN_CHANGE_INTERVAL))) {
    changesDetected++;
    readingCount++;
    
    // Отправка данных в формате JSON
    sendChangeData(currentTime, readingCount,
                   currentX, currentY, currentZ,
                   currentPitch, currentRoll, currentTilt,
                   currentAzimuth);
    
    // Отладочный вывод всех углов (по желанию)
    // printAllAngles(currentX, currentY, currentZ);
    
    // Обновление предыдущих значений
    updatePreviousValues(currentX, currentY, currentZ,
                        currentPitch, currentRoll, currentTilt,
                        currentAzimuth);
    
    lastChangeTime = currentTime;
    firstReading = false;
  }
  
  // Отправка статистики каждые 30 секунд
  static unsigned long lastStatsTime = 0;
  if (currentTime - lastStatsTime >= 30000) {
    sendStatistics(currentTime);
    lastStatsTime = currentTime;
  }
  
  // Обработка команд
  if (Serial.available()) {
    handleCommand();
  }
  
  delay(20); // ~50 Гц
}

/**
 * Расчет угла Pitch (тангаж) - наклон вперед/назад вокруг оси Y
 */
float calculatePitch(int x, int y, int z) {
  float denominator = sqrt((float)y*y + (float)z*z);
  
  if (denominator == 0) {
    return (x > 0) ? -90.0 : 90.0;
  }
  
  float pitch = atan2((float)-x, denominator) * 180.0 / PI;
  
  // Ограничение диапазона
  if (pitch > 90.0) pitch = 90.0;
  if (pitch < -90.0) pitch = -90.0;
  
  return pitch;
}

/**
 * Расчет угла Roll (крен) - наклон влево/вправо вокруг оси X
 */
float calculateRoll(int x, int y, int z) {
  float denominator = sqrt((float)x*x + (float)z*z);
  
  if (denominator == 0) {
    return (y > 0) ? 90.0 : -90.0;
  }
  
  float roll = atan2((float)y, denominator) * 180.0 / PI;
  
  // Ограничение диапазона
  if (roll > 90.0) roll = 90.0;
  if (roll < -90.0) roll = -90.0;
  
  return roll;
}

/**
 * Расчет общего угла наклона (Tilt) относительно вертикали
 */
float calculateTilt(int x, int y, int z) {
  float xyMagnitude = sqrt((float)x*x + (float)y*y);
  
  if (z == 0) {
    return 90.0;
  }
  
  float tilt = atan2(xyMagnitude, (float)z) * 180.0 / PI;
  
  // Нормализация в диапазон 0-180°
  if (tilt < 0) {
    tilt += 180.0;
  }
  
  // Ограничение диапазона
  if (tilt > 180.0) tilt = 180.0;
  if (tilt < 0.0) tilt = 0.0;
  
  return tilt;
}

/**
 * Расчет угла курса (Heading) по осям X и Y
 */
float calculateHeading(int x, int y) {
  float heading = atan2((float)y, (float)x) * 180.0 / PI;
  
  // Конвертируем в диапазон 0-360°
  if (heading < 0) {
    heading += 360.0;
  }
  
  // Применяем магнитное склонение
  heading += MAGNETIC_DECLINATION;
  
  // Нормализация
  if (heading >= 360.0) {
    heading -= 360.0;
  }
  if (heading < 0) {
    heading += 360.0;
  }
  
  return heading;
}

/**
 * Расчет азимута с компенсацией наклона
 */
float calculateTiltCompensatedHeading(int x, int y, int z, float pitch, float roll) {
  // Конвертируем углы в радианы
  float pitchRad = pitch * PI / 180.0;
  float rollRad = roll * PI / 180.0;
  
  // Компенсируем наклон
  float xh = (float)x * cos(pitchRad) + (float)z * sin(pitchRad);
  float yh = (float)x * sin(rollRad) * sin(pitchRad) + 
              (float)y * cos(rollRad) - 
              (float)z * sin(rollRad) * cos(pitchRad);
  
  // Вычисляем скомпенсированный курс
  float heading = atan2(yh, xh) * 180.0 / PI;
  
  // Конвертируем в диапазон 0-360°
  if (heading < 0) heading += 360.0;
  
  // Применяем магнитное склонение
  heading += MAGNETIC_DECLINATION;
  
  // Нормализация
  if (heading >= 360.0) heading -= 360.0;
  if (heading < 0) heading += 360.0;
  
  return heading;
}

/**
 * Определение направления по азимуту
 */
String getDirection(int azimuth) {
  int normalizedAzimuth = azimuth % 360;
  if (normalizedAzimuth < 0) {
    normalizedAzimuth += 360;
  }
  
  const char* directions[] = {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
  };
  
  // Исправленная строка - явное приведение к int перед операцией %
  int index = ((int)((normalizedAzimuth + 11.25) / 22.5)) % 16;
  
  return directions[index];
}

/**
 * Вывод всех углов для отладки
 */
void printAllAngles(int x, int y, int z) {
  float pitch = calculatePitch(x, y, z);
  float roll = calculateRoll(x, y, z);
  float tilt = calculateTilt(x, y, z);
  float heading = calculateHeading(x, y);
  float tiltCompHeading = calculateTiltCompensatedHeading(x, y, z, pitch, roll);
  String direction = getDirection((int)heading);
  String tiltCompDirection = getDirection((int)tiltCompHeading);
  
  Serial.println("=== ALL ANGLES ===");
  Serial.print("Pitch: ");
  Serial.print(pitch, 1);
  Serial.print("°\tRoll: ");
  Serial.print(roll, 1);
  Serial.print("°\tTilt: ");
  Serial.print(tilt, 1);
  Serial.println("°");
  
  Serial.print("Simple Heading: ");
  Serial.print(heading, 1);
  Serial.print("°\tDirection: ");
  Serial.println(direction);
  
  Serial.print("Tilt-Compensated Heading: ");
  Serial.print(tiltCompHeading, 1);
  Serial.print("°\tDirection: ");
  Serial.println(tiltCompDirection);
  
  Serial.println("==================");
}

/**
 * Отправка данных об изменении положения в формате JSON
 */
void sendChangeData(unsigned long timestamp, unsigned int count,
                    int x, int y, int z,
                    float pitch, float roll, float tilt,
                    int azimuth) {
  
  // Расчет дополнительных значений
  float simpleHeading = calculateHeading(x, y);
  float tiltCompHeading = calculateTiltCompensatedHeading(x, y, z, pitch, roll);
  String direction = getDirection((int)simpleHeading);
  String tiltCompDirection = getDirection((int)tiltCompHeading);
  
  // Определение типа изменения
  String changeType = "";
  if (abs(pitch - prevPitch) >= angleThreshold) changeType += "pitch ";
  if (abs(roll - prevRoll) >= angleThreshold) changeType += "roll ";
  if (abs(tilt - prevTilt) >= angleThreshold) changeType += "tilt ";
  if (abs(azimuth - prevAzimuth) >= azimuthThreshold) changeType += "azimuth ";
  if (abs(x - prevX) >= rawThreshold || 
      abs(y - prevY) >= rawThreshold || 
      abs(z - prevZ) >= rawThreshold) changeType += "raw";
  changeType.trim();
  
  // Формирование JSON
  Serial.print("{");
  Serial.print("\"event\":\"position_change\",");
  Serial.print("\"timestamp\":");
  Serial.print(timestamp);
  Serial.print(",\"reading\":");
  Serial.print(count);
  Serial.print(",\"change_type\":\"");
  Serial.print(changeType);
  
  // Сырые значения и изменения
  Serial.print("\",\"raw\":{\"x\":");
  Serial.print(x);
  Serial.print(",\"y\":");
  Serial.print(y);
  Serial.print(",\"z\":");
  Serial.print(z);
  Serial.print(",\"dx\":");
  Serial.print(x - prevX);
  Serial.print(",\"dy\":");
  Serial.print(y - prevY);
  Serial.print(",\"dz\":");
  Serial.print(z - prevZ);
  
  // Углы наклона
  Serial.print("},\"angles\":{\"pitch\":");
  Serial.print(pitch, 2);
  Serial.print(",\"roll\":");
  Serial.print(roll, 2);
  Serial.print(",\"tilt\":");
  Serial.print(tilt, 2);
  Serial.print(",\"dpitch\":");
  Serial.print(pitch - prevPitch, 2);
  Serial.print(",\"droll\":");
  Serial.print(roll - prevRoll, 2);
  Serial.print(",\"dtilt\":");
  Serial.print(tilt - prevTilt, 2);
  
  // Ориентация (компас)
  Serial.print("},\"orientation\":{\"simple\":");
  Serial.print(simpleHeading, 1);
  Serial.print(",\"simple_dir\":\"");
  Serial.print(direction);
  Serial.print("\",\"tilt_comp\":");
  Serial.print(tiltCompHeading, 1);
  Serial.print(",\"tilt_comp_dir\":\"");
  Serial.print(tiltCompDirection);
  Serial.print("\",\"azimuth\":");
  Serial.print(azimuth);
  Serial.print(",\"dazimuth\":");
  Serial.print(azimuth - prevAzimuth);
  
  // Статус положения
  Serial.print("},\"status\":\"");
  if (abs(pitch) < 3 && abs(roll) < 3) {
    Serial.print("LEVEL");
  } else if (abs(pitch) > 45 || abs(roll) > 45) {
    Serial.print("EXTREME_TILT");
  } else {
    Serial.print("TILTED");
  }
  
  Serial.println("\"}");
}

/**
 * Обновление предыдущих значений
 */
void updatePreviousValues(int x, int y, int z,
                         float pitch, float roll, float tilt,
                         int azimuth) {
  prevX = x;
  prevY = y;
  prevZ = z;
  prevPitch = pitch;
  prevRoll = roll;
  prevTilt = tilt;
  prevAzimuth = azimuth;
}

/**
 * Отправка статистики
 */
void sendStatistics(unsigned long timestamp) {
  float changeRate = (totalReadings > 0) ? (changesDetected * 100.0 / totalReadings) : 0;
  
  Serial.print("{");
  Serial.print("\"event\":\"stats\",");
  Serial.print("\"timestamp\":");
  Serial.print(timestamp);
  Serial.print(",\"total_readings\":");
  Serial.print(totalReadings);
  Serial.print(",\"changes_detected\":");
  Serial.print(changesDetected);
  Serial.print(",\"change_rate\":");
  Serial.print(changeRate, 2);
  Serial.print(",\"current_state\":{\"pitch\":");
  Serial.print(prevPitch, 2);
  Serial.print(",\"roll\":");
  Serial.print(prevRoll, 2);
  Serial.print(",\"simple_heading\":");
  Serial.print(calculateHeading(prevX, prevY), 1);
  Serial.print(",\"tilt_comp_heading\":");
  Serial.print(calculateTiltCompensatedHeading(prevX, prevY, prevZ, prevPitch, prevRoll), 1);
  Serial.print(",\"last_change_ms\":");
  Serial.print(millis() - lastChangeTime);
  Serial.println("}}");
}

/**
 * Обработка команд с Serial порта
 */
void handleCommand() {
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  
  if (cmd == "status") {
    Serial.print("{\"command\":\"status\",");
    Serial.print("\"current\":{\"pitch\":");
    Serial.print(prevPitch, 2);
    Serial.print(",\"roll\":");
    Serial.print(prevRoll, 2);
    Serial.print(",\"tilt\":");
    Serial.print(prevTilt, 2);
    Serial.print(",\"simple_heading\":");
    Serial.print(calculateHeading(prevX, prevY), 1);
    Serial.print(",\"tilt_comp_heading\":");
    Serial.print(calculateTiltCompensatedHeading(prevX, prevY, prevZ, prevPitch, prevRoll), 1);
    Serial.print(",\"direction\":\"");
    Serial.print(getDirection((int)calculateHeading(prevX, prevY)));
    Serial.print("\"},\"thresholds\":{\"angle\":");
    Serial.print(angleThreshold, 1);
    Serial.print(",\"azimuth\":");
    Serial.print(azimuthThreshold);
    Serial.print(",\"raw\":");
    Serial.print(rawThreshold);
    Serial.print("},\"stats\":{\"total_readings\":");
    Serial.print(totalReadings);
    Serial.print(",\"changes\":");
    Serial.print(changesDetected);
    Serial.println("}}");
  }
  else if (cmd == "single") {
    // Принудительная отправка текущих данных
    readingCount++;
    sendChangeData(
      millis(), readingCount,
      prevX, prevY, prevZ,
      prevPitch, prevRoll, prevTilt,
      prevAzimuth
    );
  }
  else if (cmd == "angles") {
    // Вывод всех углов для отладки
    printAllAngles(prevX, prevY, prevZ);
  }
  else if (cmd == "reset_stats") {
    totalReadings = 0;
    changesDetected = 0;
    readingCount = 0;
    Serial.println("{\"command\":\"reset_stats\",\"status\":\"ok\"}");
  }
  else if (cmd == "thresholds") {
    Serial.print("{\"command\":\"thresholds\",\"current\":{\"angle\":");
    Serial.print(angleThreshold, 2);
    Serial.print(",\"azimuth\":");
    Serial.print(azimuthThreshold);
    Serial.print(",\"raw\":");
    Serial.print(rawThreshold);
    Serial.println("}}");
  }
  else if (cmd.startsWith("threshold angle ")) {
    angleThreshold = cmd.substring(16).toFloat();
    Serial.print("{\"command\":\"set_threshold\",\"type\":\"angle\",\"value\":");
    Serial.print(angleThreshold, 2);
    Serial.println("}");
  }
  else if (cmd.startsWith("threshold azimuth ")) {
    azimuthThreshold = cmd.substring(18).toInt();
    Serial.print("{\"command\":\"set_threshold\",\"type\":\"azimuth\",\"value\":");
    Serial.print(azimuthThreshold);
    Serial.println("}");
  }
  else if (cmd.startsWith("threshold raw ")) {
    rawThreshold = cmd.substring(14).toInt();
    Serial.print("{\"command\":\"set_threshold\",\"type\":\"raw\",\"value\":");
    Serial.print(rawThreshold);
    Serial.println("}");
  }
  else if (cmd == "help") {
    Serial.println("{\"commands\":[");
    Serial.println("  \"status - текущий статус и статистика\",");
    Serial.println("  \"single - отправить текущие данные\",");
    Serial.println("  \"angles - вывести все углы для отладки\",");
    Serial.println("  \"reset_stats - сбросить статистику\",");
    Serial.println("  \"thresholds - показать текущие пороги\",");
    Serial.println("  \"threshold angle X - установить порог углов\",");
    Serial.println("  \"threshold azimuth X - установить порог азимута\",");
    Serial.println("  \"threshold raw X - установить порог сырых значений\",");
    Serial.println("  \"help - эта справка\"");
    Serial.println("]}");
  }
  else {
    Serial.print("{\"command\":\"");
    Serial.print(cmd);
    Serial.println("\",\"status\":\"unknown_command\"}");
  }
}
