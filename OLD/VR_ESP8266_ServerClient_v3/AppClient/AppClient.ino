#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char* apSSID = "MyApp";
const char* apPassword = "12345678";

ESP8266WebServer server(80);

void handleRoot() {
  String html = "<html><head><meta charset='UTF-8'>";
  html += "<title>ESP8266 WiFi Test</title></head>";
  html += "<body><h1>ESP8266 успешно подключен к WiFi!</h1>";
  html += "<p>SSID: " + String(apSSID) + "</p>";
  html += "<p>IP адрес: " + WiFi.localIP().toString() + "</p>";
  html += "<p>Сигнал: " + String(WiFi.RSSI()) + " dBm</p>";
  html += "</body></html>";
  
  server.send(200, "text/html; charset=UTF-8", html);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Подключение к WiFi...");
  WiFi.begin(apSSID, apPassword);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi подключен!");
  Serial.print("IP адрес: ");
  Serial.println(WiFi.localIP());
  
  // Запуск веб-сервера
  server.on("/", handleRoot);
  server.begin();
  Serial.println("Веб-сервер запущен");
}

void loop() {
  server.handleClient();
}
