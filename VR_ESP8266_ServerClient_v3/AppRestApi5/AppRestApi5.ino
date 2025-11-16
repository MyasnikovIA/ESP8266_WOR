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
    WiFiClient client = wifiManager.getServer().client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    
    client.print("Custom route - Device is working!");
    client.stop();
  });
  
  // Добавление маршрута для проверки статуса
  wifiManager.getServer().on("/api/status", HTTP_GET, []() {
    String status = "{";
    status += "\"ap_mode\":" + String(wifiManager.isAPModeEnabled() ? "true" : "false") + ",";
    status += "\"client_mode\":" + String(wifiManager.isClientModeEnabled() ? "true" : "false") + ",";
    status += "\"connected\":" + String(wifiManager.isConnected() ? "true" : "false") + ",";
    status += "\"ap_ip\":\"" + wifiManager.getAPIP() + "\",";
    status += "\"local_ip\":\"" + wifiManager.getLocalIP() + "\"";
    status += "}";
    
    WiFiClient client = wifiManager.getServer().client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    
    client.print(status);
    client.stop();
  });
  
  Serial.println("✓ Custom routes added");
  Serial.println("  Available at: http://" + wifiManager.getAPIP() + "/api/custom");
  Serial.println("  Available at: http://" + wifiManager.getAPIP() + "/api/status");
}

void loop() {
  // Обработка клиентов веб-сервера (неблокирующая)
  wifiManager.handleClient();
  
  // Обновление состояния WiFi модуля
  wifiManager.update();
  
  // Ваш основной код loop
  // Можно добавить свою логику здесь
  
  delay(10);
}
