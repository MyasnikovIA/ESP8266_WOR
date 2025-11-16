#include "Wifi_ESP8266.h"



void setup() {
  // Инициализация WiFi модуля
  wifiManager.begin();
  
  // Ваш остальной код setup
  // ...
}

void loop() {
  // Обработка клиентов веб-сервера (неблокирующая)
  wifiManager.handleClient();
  
  // Обновление состояния WiFi модуля
  wifiManager.update();
  
  // Ваш основной код loop
  // ...

  
  // Пример использования геттеров
  if (wifiManager.isAPModeEnabled()) {
    // Точка доступа активна
    String apIP = wifiManager.getAPIP();
    // ...
  }
  
  delay(10); // Небольшая задержка для стабильности
}
