#ifndef WIFI_ESP8266_H
#define WIFI_ESP8266_H

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <WiFiUdp.h>

// Структура для хранения настроек
struct Settings {
  char ap_ssid[32] = "VR_APP_ESP";
  char ap_password[32] = "12345678";
  char device_name[32] = "ESP8266_Device";
  char device_comment[64] = "Default Comment";
  int subnet = 4;
  bool ap_mode_enabled = true;
  bool client_mode_enabled = false;
  char sta_ssid[32] = "";
  char sta_password[32] = "";
};

// Структура для информации о подключенных устройствах
struct ConnectedDevice {
  String ip;
  String mac;
  String device_name;
  String device_comment;
};

class WiFiManager {
public:
  WiFiManager();
  
  // Перегруженные методы begin
  void begin();
  void begin(const char* ap_ssid, const char* ap_password);
  void begin(const char* ap_ssid, const char* ap_password, int subnet);
  
  void handleClient();
  void update();
  
  // Геттеры для доступа к состоянию
  bool isAPModeEnabled() { return settings.ap_mode_enabled; }
  bool isClientModeEnabled() { return settings.client_mode_enabled; }
  String getLocalIP() { return WiFi.localIP().toString(); }
  String getAPIP() { return WiFi.softAPIP().toString(); }
  bool isConnected() { return WiFi.status() == WL_CONNECTED; }
  
  // Публичный доступ к server
  ESP8266WebServer& getServer() { return server; }
  
private:
  Settings settings;
  ESP8266WebServer server;
  
  ConnectedDevice connectedDevices[10];
  int connectedDevicesCount = 0;
  
  // Адреса в EEPROM для хранения настроек
  const int EEPROM_SIZE = 512;
  const int SETTINGS_ADDR = 0;
  
  void setupWiFi();
  void connectToWiFi();
  void disconnectFromWiFi();
  void setupWebServer();
  void loadSettings();
  void saveSettings();
  void updateConnectedDevices();
  String macToString(uint8_t* mac);
  
  // Вспомогательные методы для JSON
  String escapeJSON(const String& str);
  String buildJSONResponse(const String& status, const String& message = "");
  String buildSettingsJSON();
  String buildWifiScanJSON();
  String buildConnectedDevicesJSON();
  String buildWifiStatusJSON();
  
  // Обработчики веб-сервера
  void handleRoot();
  void handleGetSettings();
  void handlePostSettings();
  void handleApiWifiScan();
  void handleApiWifiConnect();
  void handleApiWifiDisconnect();
  void handleApiWifiStatus();
  void handleApiConnectedDevices();
  void handleApiDeviceInfo();
  void handleApiClearSettings();
  void handleApiRestart();
  void handleNotFound();
  
  // Парсинг JSON без библиотеки
  bool parseSettingsJSON(const String& json);
  bool parseWifiConnectJSON(const String& json);
  bool parseDeviceInfoJSON(const String& json);
  
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
  
  // Вспомогательные методы для парсинга
  void processKeyValue(const String& key, const String& value, bool& changed);
};

extern WiFiManager wifiManager;

#endif
