#include "Wifi_ESP8266.h"

// Ваши собственные обработчики
void handleGetSettings() {
  // Ваша кастомная логика для получения настроек
  String response = "{\"custom\":\"settings\"}";
  wifiManager.getServer().send(200, "application/json", response);
}

void handleCustomRoute() {
  // Обработка кастомного маршрута
  wifiManager.getServer().send(200, "text/plain", "Custom route response");
}

void setup() {
  // Инициализация WiFi модуля
  wifiManager.begin();
  
  // Добавление кастомных маршрутов к server из основного скетча
  wifiManager.getServer().on("/api/custom-settings", HTTP_GET, handleGetSettings);
  wifiManager.getServer().on("/custom", HTTP_GET, handleCustomRoute);
  
  // Ваш остальной код setup
  // ...
}

void loop() {
  // Обработка клиентов веб-сервера (неблокирующая)
  wifiManager.handleClient();
  
  // Обновление состояния WiFi модуля
  wifiManager.update();
  
  // Пример использования доступа к server
  WiFiClient client = wifiManager.getServer().client();
  // Работа с клиентом...
  
  // Ваш основной код loop
  // ...
  
  delay(10); // Небольшая задержка для стабильности
}
