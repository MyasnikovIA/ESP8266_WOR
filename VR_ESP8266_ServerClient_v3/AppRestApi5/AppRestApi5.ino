#include "Wifi_ESP8266.h"

void setup() {
  // 1. Инициализация с настройками по умолчанию
  // wifiManager.begin();
  
  // 2. Инициализация с именем и паролем точки доступа
  wifiManager.begin("MyApp", "MyPassword123");
  
  // 3. Инициализация с именем, паролем и подсетью
  // wifiManager.begin("MyApp", "MyPassword123", 10);  // 192.168.10.1
  
  // Добавление кастомных маршрутов
  wifiManager.getServer().on("/api/custom", HTTP_GET, []() {
    wifiManager.getServer().send(200, "text/plain", "Custom route");
  });
  
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
  
  delay(10);
}
