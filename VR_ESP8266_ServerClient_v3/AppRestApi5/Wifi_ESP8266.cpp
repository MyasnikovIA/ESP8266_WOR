#include "Wifi_ESP8266.h"

const int EEPROM_SIZE = 512;
const int SETTINGS_ADDR = 0;

WiFiManager wifiManager;

WiFiManager::WiFiManager() : server(80), webSocketServer(81), connectedDevicesCount(0) {
  // Конструктор
}

void WiFiManager::begin() {
  loadDefaultSettings();
  loadSettings();
  setupWiFi();
  setupWebServer();
  
  // Инициализация WebSocket сервера
  webSocketServer.begin();
  webSocketServer.onEvent(std::bind(&WiFiManager::handleWebSocketEvent, this, 
                                  std::placeholders::_1, std::placeholders::_2, 
                                  std::placeholders::_3, std::placeholders::_4));
  
  Serial.println("WiFi Manager started");
  Serial.println("WebSocket server started on port 81");
}

void WiFiManager::begin(const char* ap_ssid, const char* ap_password) {
  loadDefaultSettings();
  strlcpy(settings.ap_ssid, ap_ssid, sizeof(settings.ap_ssid));
  strlcpy(settings.ap_password, ap_password, sizeof(settings.ap_password));
  saveSettings();
  setupWiFi();
  setupWebServer();
  
  webSocketServer.begin();
  webSocketServer.onEvent(std::bind(&WiFiManager::handleWebSocketEvent, this, 
                                  std::placeholders::_1, std::placeholders::_2, 
                                  std::placeholders::_3, std::placeholders::_4));
  
  Serial.println("WiFi Manager started with custom SSID");
  Serial.println("WebSocket server started on port 81");
}

void WiFiManager::begin(const char* ap_ssid, const char* ap_password, int subnet) {
  loadDefaultSettings();
  strlcpy(settings.ap_ssid, ap_ssid, sizeof(settings.ap_ssid));
  strlcpy(settings.ap_password, ap_password, sizeof(settings.ap_password));
  settings.subnet = subnet;
  saveSettings();
  setupWiFi();
  setupWebServer();
  
  webSocketServer.begin();
  webSocketServer.onEvent(std::bind(&WiFiManager::handleWebSocketEvent, this, 
                                  std::placeholders::_1, std::placeholders::_2, 
                                  std::placeholders::_3, std::placeholders::_4));
  
  Serial.println("WiFi Manager started with custom SSID and subnet");
  Serial.println("WebSocket server started on port 81");
}

void WiFiManager::handleClient() {
  server.handleClient();
  webSocketServer.loop();
}

void WiFiManager::update() {
  // Метод для периодического обновления состояния
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 5000) {
    updateConnectedDevices();
    processWebSocketLoopHandlers();
    lastUpdate = millis();
  }
}

// WebSocket методы
void WiFiManager::webSocket(const String& path, WebSocketCallback callback) {
  WebSocketHandler handler;
  handler.path = path;
  handler.callback = callback;
  handler.useLoopMode = false;
  webSocketHandlers.push_back(handler);
  
  Serial.println("WebSocket handler registered for path: " + path);
}

void WiFiManager::webSocketLoop(const String& path, WebSocketLoopCallback loopCallback) {
  WebSocketHandler handler;
  handler.path = path;
  handler.loopCallback = loopCallback;
  handler.useLoopMode = true;
  webSocketHandlers.push_back(handler);
  
  Serial.println("WebSocket loop handler registered for path: " + path);
}

void WiFiManager::sendWebSocketMessage(uint8_t num, const String& message) {
  if (num < 10 && webSocketServer.connectedClients() > num) {
    String msg = message; // Создаем неконстантную копию
    webSocketServer.sendTXT(num, msg);
  }
}

void WiFiManager::sendWebSocketBroadcast(const String& message) {
  String msg = message; // Создаем неконстантную копию
  webSocketServer.broadcastTXT(msg);
}

String WiFiManager::readWebSocketCommand(uint8_t num) {
  if (num < 10 && !webSocketCommands[num].empty()) {
    String command = webSocketCommands[num].front();
    webSocketCommands[num].erase(webSocketCommands[num].begin());
    return command;
  }
  return "";
}

bool WiFiManager::isWebSocketClientConnected(uint8_t num) {
  return (num < 10 && webSocketServer.connectedClients() > num);
}

void WiFiManager::disconnectWebSocketClient(uint8_t num) {
  if (num < 10) {
    webSocketServer.disconnect(num);
  }
}

void WiFiManager::handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      // Очищаем буфер команд при отключении
      if (num < 10) {
        webSocketCommands[num].clear();
      }
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocketServer.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        
        // Отправляем приветственное сообщение
        webSocketServer.sendTXT(num, "Connected to ESP8266 WebSocket");
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("[%u] Received: %s\n", num, message);
        
        // Сохраняем команду в буфер
        if (num < 10) {
          webSocketCommands[num].push_back(message);
        }
        
        // Обрабатываем команду через зарегистрированные обработчики
        for (auto& handler : webSocketHandlers) {
          if (!handler.useLoopMode && handler.callback) {
            String response = handler.callback(message);
            if (response.length() > 0) {
              webSocketServer.sendTXT(num, response);
            }
          }
        }
      }
      break;
      
    case WStype_ERROR:
      Serial.printf("[%u] Error!\n", num);
      break;
      
    case WStype_BIN:
      Serial.printf("[%u] Received binary length: %u\n", num, length);
      break;
      
    case WStype_PING:
    case WStype_PONG:
      // Игнорируем ping/pong
      break;
      
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
      // Фрагментированные сообщения не поддерживаются
      break;
  }
}

void WiFiManager::processWebSocketLoopHandlers() {
  for (auto& handler : webSocketHandlers) {
    if (handler.useLoopMode && handler.loopCallback) {
      handler.loopCallback();
    }
  }
}

bool WiFiManager::connectToWiFi(const char* ssid, const char* password) {
  if (strlen(ssid) == 0) return false;
  
  Serial.println("Connecting to WiFi: " + String(ssid));
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.println("IP address: " + WiFi.localIP().toString());
    
    // Сохраняем учетные данные
    strlcpy(settings.sta_ssid, ssid, sizeof(settings.sta_ssid));
    strlcpy(settings.sta_password, password, sizeof(settings.sta_password));
    settings.client_mode_enabled = true;
    saveSettings();
    
    return true;
  } else {
    Serial.println("\nFailed to connect to WiFi");
    return false;
  }
}

void WiFiManager::disconnectFromWiFi() {
  WiFi.disconnect();
  delay(1000);
  
  // Очищаем сохраненные учетные данные
  memset(settings.sta_ssid, 0, sizeof(settings.sta_ssid));
  memset(settings.sta_password, 0, sizeof(settings.sta_password));
  settings.client_mode_enabled = false;
  saveSettings();
  
  Serial.println("Disconnected from WiFi");
}

bool WiFiManager::scanNetworks(JsonArray& networks) {
  int n = WiFi.scanNetworks();
  if (n == 0) return false;
  
  for (int i = 0; i < n; i++) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["encryption"] = WiFi.encryptionType(i);
  }
  
  return true;
}

void WiFiManager::updateConnectedDevices() {
  connectedDevicesCount = 0;
  
  // Получаем список подключенных станций
  struct station_info *station_list = wifi_softap_get_station_info();
  struct station_info *station = station_list;
  
  while (station != NULL && connectedDevicesCount < 10) {
    connectedDevices[connectedDevicesCount].ip = IPAddress(station->ip).toString();
    connectedDevices[connectedDevicesCount].mac = macToString(station->bssid);
    connectedDevicesCount++;
    station = STAILQ_NEXT(station, next);
  }
  
  wifi_softap_free_station_info();
}

String WiFiManager::getWiFiStatus() {
  DynamicJsonDocument doc(256);
  
  if (WiFi.status() == WL_CONNECTED) {
    doc["connected"] = true;
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
  } else {
    doc["connected"] = false;
  }
  
  String response;
  serializeJson(doc, response);
  return response;
}

void WiFiManager::setupWiFi() {
  if (settings.ap_mode_enabled) {
    setupAPMode();
  } else {
    WiFi.mode(WIFI_STA);
  }
  
  // Подключаемся к WiFi сети если указаны учетные данные
  if (settings.client_mode_enabled && strlen(settings.sta_ssid) > 0) {
    connectToWiFi(settings.sta_ssid, settings.sta_password);
  }
}

void WiFiManager::setupAPMode() {
  String ap_ssid = String(settings.ap_ssid);
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(
    IPAddress(192, 168, settings.subnet, 1),
    IPAddress(192, 168, settings.subnet, 1),
    IPAddress(255, 255, 255, 0)
  );
  WiFi.softAP(ap_ssid.c_str(), settings.ap_password);
  
  Serial.println("AP Mode Started");
  Serial.println("SSID: " + ap_ssid);
  Serial.println("IP: 192.168." + String(settings.subnet) + ".1");
}

void WiFiManager::setupWebServer() {
  // Основные маршруты
  server.on("/", std::bind(&WiFiManager::handleRoot, this));
  server.on("/settings", std::bind(&WiFiManager::handleRoot, this));
  server.on("/wifi-scan", std::bind(&WiFiManager::handleRoot, this));
  server.on("/device-config", std::bind(&WiFiManager::handleRoot, this));
  server.on("/device-control", std::bind(&WiFiManager::handleRoot, this));
  
  // API маршруты WiFi
  server.on("/api/wifi-scan", HTTP_GET, [this]() {
    DynamicJsonDocument doc(2048);
    JsonArray networks = doc.createNestedArray("networks");
    
    if (this->scanNetworks(networks)) {
      String response;
      serializeJson(doc, response);
      
      server.send(200, "application/json", response);
    } else {
      server.send(200, "application/json", "{\"networks\":[]}");
    }
  });
  
  server.on("/api/wifi-connect", HTTP_POST, [this]() {
    if (server.hasArg("plain")) {
      DynamicJsonDocument doc(256);
      deserializeJson(doc, server.arg("plain"));
      
      String ssid = doc["ssid"];
      String password = doc["password"];
      
      if (this->connectToWiFi(ssid.c_str(), password.c_str())) {
        server.send(200, "application/json", "{\"status\":\"connected\"}");
      } else {
        server.send(400, "application/json", "{\"error\":\"connection_failed\"}");
      }
    } else {
      server.send(400, "application/json", "{\"error\":\"no_data\"}");
    }
  });
  
  server.on("/api/wifi-disconnect", HTTP_POST, [this]() {
    this->disconnectFromWiFi();
    server.send(200, "application/json", "{\"status\":\"disconnected\"}");
  });
  
  server.on("/api/wifi-status", HTTP_GET, [this]() {
    server.send(200, "application/json", this->getWiFiStatus());
  });
  
  // API маршруты настроек и устройств
  server.on("/api/settings", HTTP_GET, std::bind(&WiFiManager::handleGetSettings, this));
  server.on("/api/settings", HTTP_POST, std::bind(&WiFiManager::handlePostSettings, this));
  server.on("/api/connected-devices", HTTP_GET, std::bind(&WiFiManager::handleApiConnectedDevices, this));
  server.on("/api/device-info", HTTP_POST, std::bind(&WiFiManager::handleApiDeviceInfo, this));
  server.on("/api/clear-settings", HTTP_POST, std::bind(&WiFiManager::handleApiClearSettings, this));
  server.on("/api/restart", HTTP_POST, std::bind(&WiFiManager::handleApiRestart, this));
  
  // WebSocket тестовый маршрут
  server.on("/websocket-test", HTTP_GET, [this]() {
    String html = "<!DOCTYPE html>\n";
    html += "<html>\n";
    html += "<head>\n";
    html += "    <title>WebSocket Test</title>\n";
    html += "    <meta charset=\"UTF-8\">\n";
    html += "    <style>\n";
    html += "        body { font-family: Arial, sans-serif; margin: 20px; }\n";
    html += "        .container { max-width: 600px; margin: 0 auto; }\n";
    html += "        .status { padding: 10px; margin: 10px 0; border-radius: 4px; }\n";
    html += "        .connected { background-color: #d4edda; color: #155724; }\n";
    html += "        .disconnected { background-color: #f8d7da; color: #721c24; }\n";
    html += "        #messages { border: 1px solid #ccc; height: 300px; overflow-y: scroll; padding: 10px; margin: 10px 0; }\n";
    html += "        input, button { padding: 8px; margin: 5px; }\n";
    html += "    </style>\n";
    html += "</head>\n";
    html += "<body>\n";
    html += "    <div class=\"container\">\n";
    html += "        <h1>WebSocket Test</h1>\n";
    html += "        <div id=\"status\" class=\"status disconnected\">Disconnected</div>\n";
    html += "        <div>\n";
    html += "            <input type=\"text\" id=\"messageInput\" placeholder=\"Enter message\" style=\"width: 300px;\">\n";
    html += "            <button onclick=\"sendMessage()\">Send</button>\n";
    html += "            <button onclick=\"connectWS()\">Connect</button>\n";
    html += "            <button onclick=\"disconnectWS()\">Disconnect</button>\n";
    html += "        </div>\n";
    html += "        <div id=\"messages\"></div>\n";
    html += "    </div>\n";
    html += "    <script>\n";
    html += "        let ws = null;\n";
    html += "        \n";
    html += "        function connectWS() {\n";
    html += "            const wsUrl = 'ws://' + window.location.hostname + ':81/api/web_socket';\n";
    html += "            ws = new WebSocket(wsUrl);\n";
    html += "            \n";
    html += "            ws.onopen = function() {\n";
    html += "                document.getElementById('status').className = 'status connected';\n";
    html += "                document.getElementById('status').textContent = 'Connected';\n";
    html += "                addMessage('System: Connected to WebSocket');\n";
    html += "            };\n";
    html += "            \n";
    html += "            ws.onclose = function() {\n";
    html += "                document.getElementById('status').className = 'status disconnected';\n";
    html += "                document.getElementById('status').textContent = 'Disconnected';\n";
    html += "                addMessage('System: Disconnected');\n";
    html += "            };\n";
    html += "            \n";
    html += "            ws.onmessage = function(event) {\n";
    html += "                addMessage('Server: ' + event.data);\n";
    html += "            };\n";
    html += "            \n";
    html += "            ws.onerror = function(error) {\n";
    html += "                addMessage('Error: ' + error);\n";
    html += "            };\n";
    html += "        }\n";
    html += "        \n";
    html += "        function disconnectWS() {\n";
    html += "            if (ws) {\n";
    html += "                ws.close();\n";
    html += "                ws = null;\n";
    html += "            }\n";
    html += "        }\n";
    html += "        \n";
    html += "        function sendMessage() {\n";
    html += "            if (ws && ws.readyState === WebSocket.OPEN) {\n";
    html += "                const message = document.getElementById('messageInput').value;\n";
    html += "                ws.send(message);\n";
    html += "                addMessage('You: ' + message);\n";
    html += "                document.getElementById('messageInput').value = '';\n";
    html += "            } else {\n";
    html += "                addMessage('Error: Not connected');\n";
    html += "            }\n";
    html += "        }\n";
    html += "        \n";
    html += "        function addMessage(message) {\n";
    html += "            const messages = document.getElementById('messages');\n";
    html += "            messages.innerHTML += '<div>' + message + '</div>';\n";
    html += "            messages.scrollTop = messages.scrollHeight;\n";
    html += "        }\n";
    html += "        \n";
    html += "        // Auto connect\n";
    html += "        connectWS();\n";
    html += "    </script>\n";
    html += "</body>\n";
    html += "</html>";
    
    server.send(200, "text/html", html);
  });
  
  server.onNotFound(std::bind(&WiFiManager::handleNotFound, this));
  
  server.begin();
  Serial.println("HTTP server started with full web interface");
}

void WiFiManager::loadDefaultSettings() {
  strlcpy(settings.ap_ssid, "VR_APP_ESP", sizeof(settings.ap_ssid));
  strlcpy(settings.ap_password, "12345678", sizeof(settings.ap_password));
  strlcpy(settings.device_name, "ESP8266_Device", sizeof(settings.device_name));
  strlcpy(settings.device_comment, "Default Comment", sizeof(settings.device_comment));
  settings.subnet = 4;
  settings.ap_mode_enabled = true;
  settings.client_mode_enabled = false;
  memset(settings.sta_ssid, 0, sizeof(settings.sta_ssid));
  memset(settings.sta_password, 0, sizeof(settings.sta_password));
}

void WiFiManager::loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(SETTINGS_ADDR, settings);
  
  // Проверка валидности загруженных настроек
  if (settings.subnet < 1 || settings.subnet > 255) {
    settings.subnet = 4;
  }
}

void WiFiManager::saveSettings() {
  EEPROM.put(SETTINGS_ADDR, settings);
  EEPROM.commit();
}

void WiFiManager::clearSettings() {
  loadDefaultSettings();
  saveSettings();
}

String WiFiManager::macToString(uint8_t* mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

// HTML генерация (добавляем WebSocket JavaScript)
String WiFiManager::getWebSocketJavaScript() {
  String js = "<script>\n";
  js += "        // WebSocket функции для основной страницы\n";
  js += "        let mainWS = null;\n";
  js += "        \n";
  js += "        function connectMainWebSocket() {\n";
  js += "            const wsUrl = 'ws://' + window.location.hostname + ':81/api/web_socket';\n";
  js += "            mainWS = new WebSocket(wsUrl);\n";
  js += "            \n";
  js += "            mainWS.onopen = function() {\n";
  js += "                console.log('Main WebSocket connected');\n";
  js += "                updateWebSocketStatus('connected');\n";
  js += "            };\n";
  js += "            \n";
  js += "            mainWS.onclose = function() {\n";
  js += "                console.log('Main WebSocket disconnected');\n";
  js += "                updateWebSocketStatus('disconnected');\n";
  js += "                // Попытка переподключения через 3 секунды\n";
  js += "                setTimeout(connectMainWebSocket, 3000);\n";
  js += "            };\n";
  js += "            \n";
  js += "            mainWS.onmessage = function(event) {\n";
  js += "                console.log('WebSocket message:', event.data);\n";
  js += "                handleWebSocketMessage(event.data);\n";
  js += "            };\n";
  js += "            \n";
  js += "            mainWS.onerror = function(error) {\n";
  js += "                console.error('WebSocket error:', error);\n";
  js += "                updateWebSocketStatus('error');\n";
  js += "            };\n";
  js += "        }\n";
  js += "        \n";
  js += "        function updateWebSocketStatus(status) {\n";
  js += "            const statusElement = document.getElementById('websocketStatus');\n";
  js += "            if (statusElement) {\n";
  js += "                statusElement.textContent = 'WebSocket: ' + status;\n";
  js += "                statusElement.className = 'connection-status ' + \n";
  js += "                    (status === 'connected' ? 'connected' : 'disconnected');\n";
  js += "            }\n";
  js += "        }\n";
  js += "        \n";
  js += "        function handleWebSocketMessage(message) {\n";
  js += "            // Обработка входящих сообщений WebSocket\n";
  js += "            // Можно добавить свою логику здесь\n";
  js += "            if (message.startsWith('DEVICE_UPDATE')) {\n";
  js += "                loadConnectedDevices();\n";
  js += "            }\n";
  js += "        }\n";
  js += "        \n";
  js += "        function sendWebSocketMessage(message) {\n";
  js += "            if (mainWS && mainWS.readyState === WebSocket.OPEN) {\n";
  js += "                mainWS.send(message);\n";
  js += "            } else {\n";
  js += "                console.warn('WebSocket not connected');\n";
  js += "            }\n";
  js += "        }\n";
  js += "        \n";
  js += "        // Автоподключение при загрузке страницы\n";
  js += "        document.addEventListener('DOMContentLoaded', function() {\n";
  js += "            connectMainWebSocket();\n";
  js += "            \n";
  js += "            // Добавляем статус WebSocket в интерфейс\n";
  js += "            const statusTab = document.getElementById('Status');\n";
  js += "            if (statusTab) {\n";
  js += "                const statusElement = document.createElement('div');\n";
  js += "                statusElement.id = 'websocketStatus';\n";
  js += "                statusElement.className = 'connection-status disconnected';\n";
  js += "                statusElement.textContent = 'WebSocket: disconnected';\n";
  js += "                statusTab.insertBefore(statusElement, statusTab.firstChild);\n";
  js += "            }\n";
  js += "        });\n";
  js += "    </script>\n";
  return js;
}

String WiFiManager::getHTMLHeader() {
  String header = "<!DOCTYPE html>\n";
  header += "<html>\n";
  header += "<head>\n";
  header += "    <title>ESP8266 Configuration</title>\n";
  header += "    <meta charset=\"UTF-8\">\n";
  header += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
  header += "    <style>\n";
  header += "        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }\n";
  header += "        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n";
  header += "        .tab { overflow: hidden; border: 1px solid #ccc; background-color: #f1f1f1; border-radius: 5px 5px 0 0; }\n";
  header += "        .tab button { background-color: inherit; float: left; border: none; outline: none; cursor: pointer; padding: 14px 16px; transition: 0.3s; font-size: 17px; }\n";
  header += "        .tab button:hover { background-color: #ddd; }\n";
  header += "        .tab button.active { background-color: #4CAF50; color: white; }\n";
  header += "        .tabcontent { display: none; padding: 20px; border: 1px solid #ccc; border-top: none; border-radius: 0 0 5px 5px; }\n";
  header += "        .form-group { margin-bottom: 15px; }\n";
  header += "        label { display: block; margin-bottom: 5px; font-weight: bold; }\n";
  header += "        input, select, textarea { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }\n";
  header += "        button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; margin: 5px; }\n";
  header += "        button:hover { background-color: #45a049; }\n";
  header += "        table { width: 100%; border-collapse: collapse; margin-top: 10px; }\n";
  header += "        th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n";
  header += "        th { background-color: #f2f2f2; }\n";
  header += "        .modal { display: none; position: fixed; z-index: 1; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.4); }\n";
  header += "        .modal-content { background-color: #fefefe; margin: 15% auto; padding: 20px; border: 1px solid #888; width: 80%; max-width: 500px; border-radius: 5px; }\n";
  header += "        .close { color: #aaa; float: right; font-size: 28px; font-weight: bold; cursor: pointer; }\n";
  header += "        .close:hover { color: black; }\n";
  header += "        .wifi-network { border: 1px solid #ddd; padding: 10px; margin: 5px 0; border-radius: 4px; }\n";
  header += "        .wifi-connected { background-color: #e8f5e8; }\n";
  header += "        .wifi-disconnected { background-color: #f5f5f5; }\n";
  header += "        .tablinks{ color: black; }\n";
  header += "        .connection-status { padding: 10px; border-radius: 4px; margin: 10px 0; }\n";
  header += "        .connected { background-color: #d4edda; color: #155724; }\n";
  header += "        .disconnected { background-color: #f8d7da; color: #721c24; }\n";
  header += "    </style>\n";
  return header;
}

String WiFiManager::getJavaScript() {
  String js = getWebSocketJavaScript();
  js += "<script>\n";
  js += "        function openTab(evt, tabName) {\n";
  js += "            var i, tabcontent, tablinks;\n";
  js += "            tabcontent = document.getElementsByClassName(\"tabcontent\");\n";
  js += "            for (i = 0; i < tabcontent.length; i++) {\n";
  js += "                tabcontent[i].style.display = \"none\";\n";
  js += "            }\n";
  js += "            tablinks = document.getElementsByClassName(\"tablinks\");\n";
  js += "            for (i = 0; i < tablinks.length; i++) {\n";
  js += "                tablinks[i].className = tablinks[i].className.replace(\" active\", \"\");\n";
  js += "            }\n";
  js += "            document.getElementById(tabName).style.display = \"block\";\n";
  js += "            evt.currentTarget.className += \" active\";\n";
  js += "            \n";
  js += "            if (tabName === 'Status') {\n";
  js += "                loadConnectedDevices();\n";
  js += "            } else if (tabName === 'APSettings') {\n";
  js += "                loadAPSettings();\n";
  js += "            } else if (tabName === 'WiFiScan') {\n";
  js += "                loadWiFiStatus();\n";
  js += "                scanWiFi();\n";
  js += "            } else if (tabName === 'DeviceConfig') {\n";
  js += "                loadDeviceConfig();\n";
  js += "            } else if (tabName === 'DeviceControl') {\n";
  js += "                loadDeviceControl();\n";
  js += "            }\n";
  js += "        }\n";
  js += "\n";
  js += "        function loadConnectedDevices() {\n";
  js += "            fetch('/api/connected-devices')\n";
  js += "                .then(response => response.json())\n";
  js += "                .then(data => {\n";
  js += "                    let table = '<table><tr><th>IP Адрес</th><th>MAC Адрес</th><th>Имя устройства</th><th>Комментарий</th><th>Действия</th></tr>';\n";
  js += "                    data.devices.forEach(device => {\n";
  js += "                        table += `<tr>\n";
  js += "                            <td>${device.ip}</td>\n";
  js += "                            <td>${device.mac}</td>\n";
  js += "                            <td>${device.device_name || ''}</td>\n";
  js += "                            <td>${device.device_comment || ''}</td>\n";
  js += "                            <td><button onclick=\"showDeviceInfo('${device.mac}', '${device.device_name || ''}', '${device.device_comment || ''}')\">Информация об устройстве</button></td>\n";
  js += "                        </tr>`;\n";
  js += "                    });\n";
  js += "                    table += '</table>';\n";
  js += "                    document.getElementById('connectedDevicesTable').innerHTML = table;\n";
  js += "                });\n";
  js += "        }\n";
  js += "\n";
  js += "        function showDeviceInfo(mac, name, comment) {\n";
  js += "            document.getElementById('modalDeviceMac').value = mac;\n";
  js += "            document.getElementById('modalDeviceName').value = name || '';\n";
  js += "            document.getElementById('modalDeviceComment').value = comment || '';\n";
  js += "            document.getElementById('deviceInfoModal').style.display = 'block';\n";
  js += "        }\n";
  js += "\n";
  js += "        function closeModal() {\n";
  js += "            document.getElementById('deviceInfoModal').style.display = 'none';\n";
  js += "        }\n";
  js += "\n";
  js += "        function saveDeviceInfo() {\n";
  js += "            const mac = document.getElementById('modalDeviceMac').value;\n";
  js += "            const name = document.getElementById('modalDeviceName').value;\n";
  js += "            const comment = document.getElementById('modalDeviceComment').value;\n";
  js += "            \n";
  js += "            fetch('/api/device-info', {\n";
  js += "                method: 'POST',\n";
  js += "                headers: { 'Content-Type': 'application/json' },\n";
  js += "                body: JSON.stringify({ mac: mac, device_name: name, device_comment: comment })\n";
  js += "            }).then(() => {\n";
  js += "                closeModal();\n";
  js += "                loadConnectedDevices();\n";
  js += "            });\n";
  js += "        }\n";
  js += "\n";
  js += "        function loadAPSettings() {\n";
  js += "            fetch('/api/settings')\n";
  js += "                .then(response => response.json())\n";
  js += "                .then(settings => {\n";
  js += "                    document.getElementById('apSsid').value = settings.ap_ssid || '';\n";
  js += "                    document.getElementById('apPassword').value = settings.ap_password || '';\n";
  js += "                    \n";
  js += "                    const subnetSelect = document.getElementById('subnet');\n";
  js += "                    subnetSelect.innerHTML = '';\n";
  js += "                    for (let i = 1; i <= 255; i++) {\n";
  js += "                        const option = document.createElement('option');\n";
  js += "                        option.value = i;\n";
  js += "                        option.textContent = '192.168.' + i + '.1';\n";
  js += "                        if (i === (settings.subnet || 4)) {\n";
  js += "                            option.selected = true;\n";
  js += "                        }\n";
  js += "                        subnetSelect.appendChild(option);\n";
  js += "                    }\n";
  js += "                });\n";
  js += "        }\n";
  js += "\n";
  js += "        function saveAPSettings() {\n";
  js += "            const settings = {\n";
  js += "                ap_ssid: document.getElementById('apSsid').value,\n";
  js += "                ap_password: document.getElementById('apPassword').value,\n";
  js += "                subnet: parseInt(document.getElementById('subnet').value)\n";
  js += "            };\n";
  js += "            \n";
  js += "            fetch('/api/settings', {\n";
  js += "                method: 'POST',\n";
  js += "                headers: { 'Content-Type': 'application/json' },\n";
  js += "                body: JSON.stringify(settings)\n";
  js += "            }).then(() => alert('Настройки точки доступа сохранены'));\n";
  js += "        }\n";
  js += "\n";
  js += "        function scanWiFi() {\n";
  js += "            fetch('/api/wifi-scan')\n";
  js += "                .then(response => response.json())\n";
  js += "                .then(data => {\n";
  js += "                    let networksHTML = '<h3>Доступные сети:</h3>';\n";
  js += "                    data.networks.forEach(network => {\n";
  js += "                        const encryption = network.encryption === 7 ? 'Open' : 'Secured';\n";
  js += "                        networksHTML += `\n";
  js += "                            <div class=\"wifi-network\">\n";
  js += "                                <strong>${network.ssid}</strong><br>\n";
  js += "                                Сигнал: ${network.rssi}dBm | Защита: ${encryption}<br>\n";
  js += "                                <input type=\"password\" id=\"password_${network.ssid.replace(/[^a-zA-Z0-9]/g, '_')}\" placeholder=\"Пароль\" style=\"width: 200px; margin: 5px 0;\">\n";
  js += "                                <button onclick=\"connectToNetwork('${network.ssid}', ${network.encryption})\">Подключиться</button>\n";
  js += "                            </div>\n";
  js += "                        `;\n";
  js += "                    });\n";
  js += "                    document.getElementById('wifiNetworks').innerHTML = networksHTML;\n";
  js += "                });\n";
  js += "        }\n";
  js += "\n";
  js += "        function connectToNetwork(ssid, encryption) {\n";
  js += "            const passwordId = 'password_' + ssid.replace(/[^a-zA-Z0-9]/g, '_');\n";
  js += "            const password = document.getElementById(passwordId).value;\n";
  js += "            \n";
  js += "            if (encryption !== 7 && !password) {\n";
  js += "                alert('Для защищенной сети необходим пароль');\n";
  js += "                return;\n";
  js += "            }\n";
  js += "            \n";
  js += "            const connectionData = {\n";
  js += "                ssid: ssid,\n";
  js += "                password: password\n";
  js += "            };\n";
  js += "            \n";
  js += "            fetch('/api/wifi-connect', {\n";
  js += "                method: 'POST',\n";
  js += "                headers: { 'Content-Type': 'application/json' },\n";
  js += "                body: JSON.stringify(connectionData)\n";
  js += "            })\n";
  js += "            .then(response => response.json())\n";
  js += "            .then(data => {\n";
  js += "                alert('Подключение к сети ' + ssid + ' выполнено');\n";
  js += "                loadWiFiStatus();\n";
  js += "            })\n";
  js += "            .catch(error => {\n";
  js += "                alert('Ошибка подключения к сети');\n";
  js += "            });\n";
  js += "        }\n";
  js += "\n";
  js += "        function disconnectFromWiFi() {\n";
  js += "            if (confirm('Отключиться от текущей WiFi сети?')) {\n";
  js += "                fetch('/api/wifi-disconnect', { method: 'POST' })\n";
  js += "                    .then(response => response.json())\n";
  js += "                    .then(data => {\n";
  js += "                        alert('Отключено от WiFi сети');\n";
  js += "                        loadWiFiStatus();\n";
  js += "                        scanWiFi();\n";
  js += "                    });\n";
  js += "            }\n";
  js += "        }\n";
  js += "\n";
  js += "        function loadWiFiStatus() {\n";
  js += "            fetch('/api/wifi-status')\n";
  js += "                .then(response => response.json())\n";
  js += "                .then(data => {\n";
  js += "                    let statusHTML = '';\n";
  js += "                    if (data.connected) {\n";
  js += "                        statusHTML = `\n";
  js += "                            <div class=\"connection-status connected\">\n";
  js += "                                <strong>Подключено к WiFi</strong><br>\n";
  js += "                                Сеть: ${data.ssid}<br>\n";
  js += "                                IP: ${data.ip}<br>\n";
  js += "                                Сигнал: ${data.rssi}dBm\n";
  js += "                            </div>\n";
  js += "                            <button onclick=\"disconnectFromWiFi()\" style=\"background-color: #f44336;\">Отключиться от точки доступа</button>\n";
  js += "                        `;\n";
  js += "                    } else {\n";
  js += "                        statusHTML = `\n";
  js += "                            <div class=\"connection-status disconnected\">\n";
  js += "                                <strong>Не подключено к WiFi</strong>\n";
  js += "                            </div>\n";
  js += "                        `;\n";
  js += "                    }\n";
  js += "                    document.getElementById('wifiStatus').innerHTML = statusHTML;\n";
  js += "                });\n";
  js += "        }\n";
  js += "\n";
  js += "        function loadDeviceConfig() {\n";
  js += "            fetch('/api/settings')\n";
  js += "                .then(response => response.json())\n";
  js += "                .then(settings => {\n";
  js += "                    document.getElementById('deviceName').value = settings.device_name || '';\n";
  js += "                    document.getElementById('deviceComment').value = settings.device_comment || '';\n";
  js += "                });\n";
  js += "        }\n";
  js += "\n";
  js += "        function saveDeviceConfig() {\n";
  js += "            const config = {\n";
  js += "                device_name: document.getElementById('deviceName').value,\n";
  js += "                device_comment: document.getElementById('deviceComment').value\n";
  js += "            };\n";
  js += "            \n";
  js += "            fetch('/api/settings', {\n";
  js += "                method: 'POST',\n";
  js += "                headers: { 'Content-Type': 'application/json' },\n";
  js += "                body: JSON.stringify(config)\n";
  js += "            }).then(() => alert('Настройки устройства сохранены'));\n";
  js += "        }\n";
  js += "\n";
  js += "        function loadDeviceControl() {\n";
  js += "            fetch('/api/settings')\n";
  js += "                .then(response => response.json())\n";
  js += "                .then(settings => {\n";
  js += "                    document.getElementById('apModeEnabled').checked = settings.ap_mode_enabled || false;\n";
  js += "                    document.getElementById('clientModeEnabled').checked = settings.client_mode_enabled || false;\n";
  js += "                });\n";
  js += "        }\n";
  js += "\n";
  js += "        function saveDeviceControl() {\n";
  js += "            const control = {\n";
  js += "                ap_mode_enabled: document.getElementById('apModeEnabled').checked,\n";
  js += "                client_mode_enabled: document.getElementById('clientModeEnabled').checked\n";
  js += "            };\n";
  js += "            \n";
  js += "            fetch('/api/settings', {\n";
  js += "                method: 'POST',\n";
  js += "                headers: { 'Content-Type': 'application/json' },\n";
  js += "                body: JSON.stringify(control)\n";
  js += "            }).then(() => alert('Настройки управления сохранены'));\n";
  js += "        }\n";
  js += "\n";
  js += "        function clearSettings() {\n";
  js += "            if (confirm('Вы уверены, что хотите очистить все настройки?')) {\n";
  js += "                fetch('/api/clear-settings', { method: 'POST' })\n";
  js += "                    .then(() => alert('Настройки очищены'));\n";
  js += "            }\n";
  js += "        }\n";
  js += "\n";
  js += "        function restartDevice() {\n";
  js += "            if (confirm('Перезагрузить устройство?')) {\n";
  js += "                fetch('/api/restart', { method: 'POST' });\n";
  js += "            }\n";
  js += "        }\n";
  js += "\n";
  js += "        document.addEventListener('DOMContentLoaded', function() {\n";
  js += "            loadConnectedDevices();\n";
  js += "            loadAPSettings();\n";
  js += "        });\n";
  js += "    </script>\n";
  return js;
}

String WiFiManager::getHTMLBodyStart() {
  return "</head>\n<body>\n    <div class=\"container\">\n        <h1>ESP8266 Configuration</h1>\n        \n        <div class=\"tab\">\n            <button class=\"tablinks active\" onclick=\"openTab(event, 'Status')\">Статус</button>\n            <button class=\"tablinks\" onclick=\"openTab(event, 'APSettings')\">Настройки точки доступа</button>\n            <button class=\"tablinks\" onclick=\"openTab(event, 'WiFiScan')\">Сканирование WiFi сетей</button>\n            <button class=\"tablinks\" onclick=\"openTab(event, 'DeviceConfig')\">Настройки устройства</button>\n            <button class=\"tablinks\" onclick=\"openTab(event, 'DeviceControl')\">Управление устройством</button>\n        </div>\n";
}

String WiFiManager::getStatusTab() {
  return "        <div id=\"Status\" class=\"tabcontent\" style=\"display: block;\">\n            <h2>Список подключенных устройств</h2>\n            <div id=\"connectedDevicesTable\"></div>\n        </div>\n";
}

String WiFiManager::getAPSettingsTab() {
  return "        <div id=\"APSettings\" class=\"tabcontent\">\n            <h2>Настройки точки доступа</h2>\n            <div class=\"form-group\">\n                <label for=\"apSsid\">Имя точки доступа (SSID):</label>\n                <input type=\"text\" id=\"apSsid\">\n            </div>\n            <div class=\"form-group\">\n                <label for=\"apPassword\">Пароль точки доступа:</label>\n                <input type=\"password\" id=\"apPassword\">\n            </div>\n            <div class=\"form-group\">\n                <label for=\"subnet\">Подсеть (192.168.XXX.1):</label>\n                <select id=\"subnet\"></select>\n            </div>\n            <button onclick=\"saveAPSettings()\">Сохранить настройки AP</button>\n        </div>\n";
}

String WiFiManager::getWiFiScanTab() {
  return "        <div id=\"WiFiScan\" class=\"tabcontent\">\n            <h2>Сканирование WiFi сетей</h2>\n            <div id=\"wifiStatus\"></div>\n            <button onclick=\"scanWiFi()\">Сканировать сети</button>\n            <button onclick=\"loadWiFiStatus()\">Обновить статус</button>\n            <div id=\"wifiNetworks\"></div>\n        </div>\n";
}

String WiFiManager::getDeviceConfigTab() {
  return "        <div id=\"DeviceConfig\" class=\"tabcontent\">\n            <h2>Настройки устройства</h2>\n            <div class=\"form-group\">\n                <label for=\"deviceName\">Имя устройства:</label>\n                <input type=\"text\" id=\"deviceName\">\n            </div>\n            <div class=\"form-group\">\n                <label for=\"deviceComment\">Комментарий для устройства:</label>\n                <textarea id=\"deviceComment\"></textarea>\n            </div>\n            <button onclick=\"saveDeviceConfig()\">Сохранить настройки устройства</button>\n        </div>\n";
}

String WiFiManager::getDeviceControlTab() {
  return "        <div id=\"DeviceControl\" class=\"tabcontent\">\n            <h2>Управление устройством</h2>\n            <div class=\"form-group\">\n                <label>\n                    <input type=\"checkbox\" id=\"apModeEnabled\">\n                    Режим точки доступа\n                </label>\n            </div>\n            <div class=\"form-group\">\n                <label>\n                    <input type=\"checkbox\" id=\"clientModeEnabled\">\n                    Режим клиента WiFi\n                </label>\n            </div>\n            <button onclick=\"saveDeviceControl()\">Сохранить настройки</button>\n            <button onclick=\"clearSettings()\" style=\"background-color: #f44336;\">Очистить настройки</button>\n            <button onclick=\"restartDevice()\" style=\"background-color: #ff9800;\">Перезагрузить устройство</button>\n        </div>\n";
}

String WiFiManager::getModalWindow() {
  return "        <!-- Модальное окно для информации об устройстве -->\n        <div id=\"deviceInfoModal\" class=\"modal\">\n            <div class=\"modal-content\">\n                <span class=\"close\" onclick=\"closeModal()\">&times;</span>\n                <h2>Информация об устройстве</h2>\n                <div class=\"form-group\">\n                    <label for=\"modalDeviceName\">Имя устройства:</label>\n                    <input type=\"text\" id=\"modalDeviceName\">\n                </div>\n                <div class=\"form-group\">\n                    <label for=\"modalDeviceComment\">Комментарий:</label>\n                    <textarea id=\"modalDeviceComment\"></textarea>\n                </div>\n                <input type=\"hidden\" id=\"modalDeviceMac\">\n                <button onclick=\"saveDeviceInfo()\">Сохранить</button>\n            </div>\n        </div>\n    </div>\n</body>\n</html>\n";
}

void WiFiManager::handleRoot() {
  WiFiClient client = server.client();
  
  // Отправляем HTTP заголовок
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  // Отправляем HTML частями
  client.print(getHTMLHeader());
  client.print(getJavaScript());
  client.print(getHTMLBodyStart());
  client.print(getStatusTab());
  client.print(getAPSettingsTab());
  client.print(getWiFiScanTab());
  client.print(getDeviceConfigTab());
  client.print(getDeviceControlTab());
  client.print(getModalWindow());
  
  client.stop();
}

void WiFiManager::handleGetSettings() {
  DynamicJsonDocument doc(1024);
  doc["ap_ssid"] = settings.ap_ssid;
  doc["ap_password"] = settings.ap_password;
  doc["device_name"] = settings.device_name;
  doc["device_comment"] = settings.device_comment;
  doc["subnet"] = settings.subnet;
  doc["ap_mode_enabled"] = settings.ap_mode_enabled;
  doc["client_mode_enabled"] = settings.client_mode_enabled;
  doc["sta_ssid"] = settings.sta_ssid;
  
  String response;
  serializeJson(doc, response);
  
  server.send(200, "application/json", response);
}

void WiFiManager::handlePostSettings() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, server.arg("plain"));
    
    if (doc.containsKey("ap_ssid")) strlcpy(settings.ap_ssid, doc["ap_ssid"], sizeof(settings.ap_ssid));
    if (doc.containsKey("ap_password")) strlcpy(settings.ap_password, doc["ap_password"], sizeof(settings.ap_password));
    if (doc.containsKey("device_name")) strlcpy(settings.device_name, doc["device_name"], sizeof(settings.device_name));
    if (doc.containsKey("device_comment")) strlcpy(settings.device_comment, doc["device_comment"], sizeof(settings.device_comment));
    if (doc.containsKey("subnet")) settings.subnet = doc["subnet"];
    if (doc.containsKey("ap_mode_enabled")) settings.ap_mode_enabled = doc["ap_mode_enabled"];
    if (doc.containsKey("client_mode_enabled")) settings.client_mode_enabled = doc["client_mode_enabled"];
    
    saveSettings();
    
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"No data\"}");
  }
}

void WiFiManager::handleApiConnectedDevices() {
  updateConnectedDevices();
  
  DynamicJsonDocument doc(2048);
  JsonArray devices = doc.createNestedArray("devices");
  
  for (int i = 0; i < connectedDevicesCount; i++) {
    JsonObject device = devices.createNestedObject();
    device["ip"] = connectedDevices[i].ip;
    device["mac"] = connectedDevices[i].mac;
    device["device_name"] = connectedDevices[i].device_name;
    device["device_comment"] = connectedDevices[i].device_comment;
  }
  
  String response;
  serializeJson(doc, response);
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(200, "application/json", response);
}

void WiFiManager::handleApiDeviceInfo() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, server.arg("plain"));
    
    String mac = doc["mac"];
    String device_name = doc["device_name"];
    String device_comment = doc["device_comment"];
    
    // Обновляем информацию об устройстве
    for (int i = 0; i < connectedDevicesCount; i++) {
      if (connectedDevices[i].mac == mac) {
        connectedDevices[i].device_name = device_name;
        connectedDevices[i].device_comment = device_comment;
        break;
      }
    }
    
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"No data\"}");
  }
}

void WiFiManager::handleApiClearSettings() {
  clearSettings();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void WiFiManager::handleApiRestart() {
  server.send(200, "application/json", "{\"status\":\"restarting\"}");
  delay(1000);
  ESP.restart();
}

void WiFiManager::handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  
  server.send(404, "text/plain", message);
}
