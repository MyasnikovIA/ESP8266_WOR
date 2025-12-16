#ifndef WIFI_ESP8266_H
#define WIFI_ESP8266_H

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <functional>
#include <vector>

// Типы колбэков для WebSocket
typedef std::function<String(const String&)> WebSocketCallback;
typedef std::function<void()> WebSocketLoopCallback;

// Структура для информации о подключенных устройствах
struct ConnectedDevice {
  String ip;
  String mac;
  String device_name;
  String device_comment;
};

// Структура для WebSocket обработчика
struct WebSocketHandler {
  String path;
  WebSocketCallback callback;
  WebSocketLoopCallback loopCallback;
  bool useLoopMode;
};

// Расширенная структура для хранения настроек
struct WiFiSettings {
  char ap_ssid[32];
  char ap_password[32];
  char device_name[32];
  char device_comment[64];
  int subnet;
  bool ap_mode_enabled;
  bool client_mode_enabled;
  char sta_ssid[32];
  char sta_password[32];
};

class WiFiManager {
public:
  WiFiManager();
  
  // Методы инициализации
  void begin();
  void begin(const char* ap_ssid, const char* ap_password = "12345678");
  void begin(const char* ap_ssid, const char* ap_password, int subnet);
  
  // Основные методы управления
  void handleClient();
  void update();
  
  // Методы работы с WiFi
  bool connectToWiFi(const char* ssid, const char* password);
  void disconnectFromWiFi();
  bool scanNetworks(JsonArray& networks);
  
  // Методы работы с подключенными устройствами
  void updateConnectedDevices();
  int getConnectedDevicesCount() const { return connectedDevicesCount; }
  ConnectedDevice* getConnectedDevices() { return connectedDevices; }
  
  // Методы работы с WebSocket
  void webSocket(const String& path, WebSocketCallback callback);
  void webSocketLoop(const String& path, WebSocketLoopCallback loopCallback);
  void sendWebSocketMessage(uint8_t num, const String& message);
  void sendWebSocketBroadcast(const String& message);
  String readWebSocketCommand(uint8_t num);
  bool isWebSocketClientConnected(uint8_t num);
  void disconnectWebSocketClient(uint8_t num);
  
  // Геттеры
  bool isAPModeEnabled() const { return settings.ap_mode_enabled; }
  bool isClientModeEnabled() const { return settings.client_mode_enabled; }
  bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
  String getAPIP() const { return WiFi.softAPIP().toString(); }
  String getLocalIP() const { return WiFi.localIP().toString(); }
  ESP8266WebServer& getServer() { return server; }
  WiFiSettings& getSettings() { return settings; }
  WebSocketsServer& getWebSocketServer() { return webSocketServer; }
  
  // Работа с настройками
  void loadSettings();
  void saveSettings();
  void clearSettings();
  
  // Утилиты
  String getWiFiStatus();
  String macToString(uint8_t* mac);
  
  // HTML генерация
  String getHTMLHeader();
  String getJavaScript();
  String getHTMLBodyStart();
  String getStatusTab();
  String getAPSettingsTab();
  String getWiFiScanTab();
  String getDeviceConfigTab();
  String getDeviceControlTab();
  String getModalWindow();
  String getWebSocketJavaScript();

private:
  ESP8266WebServer server;
  WebSocketsServer webSocketServer;
  WiFiSettings settings;
  ConnectedDevice connectedDevices[10];
  int connectedDevicesCount;
  
  // WebSocket обработчики
  std::vector<WebSocketHandler> webSocketHandlers;
  std::vector<String> webSocketCommands[10]; // Буфер команд для каждого клиента
  
  void setupWiFi();
  void setupWebServer();
  void setupAPMode();
  void loadDefaultSettings();
  
  // Обработчики маршрутов
  void handleRoot();
  void handleGetSettings();
  void handlePostSettings();
  void handleApiConnectedDevices();
  void handleApiDeviceInfo();
  void handleApiClearSettings();
  void handleApiRestart();
  void handleNotFound();
  
  // Обработчики WebSocket
  void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
  void processWebSocketLoopHandlers();
};

extern WiFiManager wifiManager;

#endif
