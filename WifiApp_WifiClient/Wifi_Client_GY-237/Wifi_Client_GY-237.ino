// Подключаем необходимые библиотеки
#include <Wire.h>
#include <QMC5883LCompass.h>

// Создаем объект для работы с компасом
QMC5883LCompass compass;

void setup() {
  // Запускаем последовательное соединение для вывода данных
  Serial.begin(115200);
  
  // Инициализируем шину I2C, указав пины SDA (GPIO2) и SCL (GPIO0)
  Wire.begin(2, 0);
  
  // Устанавливаем I2C-адрес датчика (для QMC5883L это 0x0D)
  compass.setADDR(0x0D);
  
  // Инициализируем компас
  compass.init();
  
  // ЗДЕСЬ БУДУТ ВАШИ КАЛИБРОВОЧНЫЕ КОЭФФИЦИЕНТЫ
  // compass.setCalibration(X_MIN, X_MAX, Y_MIN, Y_MAX, Z_MIN, Z_MAX);
}

void loop() {
  // Объявляем переменные для хранения сырых данных
  int x, y, z;
  
  // Считываем значения с датчика
  compass.read();
  
  // Получаем сырые данные по осям
  x = compass.getX();
  y = compass.getY();
  z = compass.getZ();
  
  // Выводим значения в монитор порта
  Serial.print("X: ");
  Serial.print(x);
  Serial.print(" | Y: ");
  Serial.print(y);
  Serial.print(" | Z: ");
  Serial.println(z);
  
  // Небольшая задержка между измерениями
  delay(250);
}
