#include "Wifi_ESP8266.h"

void setup() {
  Serial.begin(115200);
  
  // 1. Инициализация с настройками по умолчанию
  //wifiManager.begin();
  
  // 2. Инициализация с именем и паролем точки доступа
  wifiManager.begin("MyApp", "MyPassword123");
  
  // 3. Инициализация с именем, паролем и подсетью
  // wifiManager.begin("MyApp", "MyPassword123", 10);  // 192.168.10.1
  
  // Пример 1: Простой обработчик WebSocket
  wifiManager.webSocket("/api/web_socket", [](const String& inputCmd) {
    Serial.println("Received WebSocket command: " + inputCmd);
    
    // Обработка команды и возврат ответа
    if (inputCmd == "PING") {
      return String("PONG");
    } else if (inputCmd == "TIME") {
      return String("Time: ") + String(millis());
    } else if (inputCmd == "STATUS") {
      return String("WiFi: ") + String(wifiManager.isConnected() ? "Connected" : "Disconnected");
    } else {
      return String("Echo: ") + inputCmd;
    }
  });
  
  // Пример 2: Loop обработчик WebSocket
  wifiManager.webSocketLoop("/api/web_socket_loop", []() {
    static unsigned long lastBroadcast = 0;
    
    // Проверяем подключенных клиентов
    for (uint8_t i = 0; i < 10; i++) {
      if (wifiManager.isWebSocketClientConnected(i)) {
        // Читаем команды из буфера
        String command = wifiManager.readWebSocketCommand(i);
        if (command.length() > 0) {
          // Обрабатываем команду
          if (command == "GET_TEMP") {
            float temp = 25.0 + (random(0, 100) / 100.0); // Имитация температуры
            wifiManager.sendWebSocketMessage(i, String("TEMP:") + String(temp));
          } else if (command == "GET_HUMIDITY") {
            float humidity = 50.0 + (random(0, 100) / 100.0); // Имитация влажности
            wifiManager.sendWebSocketMessage(i, String("HUMIDITY:") + String(humidity));
          }
        }
      }
    }
    
    // Периодическая рассылка данных всем клиентам
    if (millis() - lastBroadcast > 10000) { // Каждые 10 секунд
      String broadcastMsg = String("BROADCAST: System time: ") + String(millis());
      wifiManager.sendWebSocketBroadcast(broadcastMsg);
      lastBroadcast = millis();
    }
    
    delay(100); // Небольшая пауза для экономии ресурсов
  });
  
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
    status += "\"local_ip\":\"" + wifiManager.getLocalIP() + "\",";
    status += "\"websocket_clients\":" + String(wifiManager.getWebSocketServer().connectedClients());
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
  Serial.println("✓ WebSocket handlers registered");
  Serial.println("  Available at: http://" + wifiManager.getAPIP() + "/api/custom");
  Serial.println("  Available at: http://" + wifiManager.getAPIP() + "/api/status");
  Serial.println("  Web interface: http://" + wifiManager.getAPIP());
  Serial.println("  WebSocket test: http://" + wifiManager.getAPIP() + "/websocket-test");
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
