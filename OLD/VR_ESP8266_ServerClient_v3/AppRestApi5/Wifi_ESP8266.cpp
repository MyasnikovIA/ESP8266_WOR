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
    WiFiClient client = server.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    
    client.println("<!DOCTYPE html>");
    client.println("<html>");
    client.println("<head>");
    client.println("    <title>WebSocket Test</title>");
    client.println("    <meta charset=\"UTF-8\">");
    client.println("    <style>");
    client.println("        body { font-family: Arial, sans-serif; margin: 20px; }");
    client.println("        .container { max-width: 600px; margin: 0 auto; }");
    client.println("        .status { padding: 10px; margin: 10px 0; border-radius: 4px; }");
    client.println("        .connected { background-color: #d4edda; color: #155724; }");
    client.println("        .disconnected { background-color: #f8d7da; color: #721c24; }");
    client.println("        #messages { border: 1px solid #ccc; height: 300px; overflow-y: scroll; padding: 10px; margin: 10px 0; }");
    client.println("        input, button { padding: 8px; margin: 5px; }");
    client.println("    </style>");
    client.println("</head>");
    client.println("<body>");
    client.println("    <div class=\"container\">");
    client.println("        <h1>WebSocket Test</h1>");
    client.println("        <div id=\"status\" class=\"status disconnected\">Disconnected</div>");
    client.println("        <div>");
    client.println("            <input type=\"text\" id=\"messageInput\" placeholder=\"Enter message\" style=\"width: 300px;\">");
    client.println("            <button onclick=\"sendMessage()\">Send</button>");
    client.println("            <button onclick=\"connectWS()\">Connect</button>");
    client.println("            <button onclick=\"disconnectWS()\">Disconnect</button>");
    client.println("        </div>");
    client.println("        <div id=\"messages\"></div>");
    client.println("    </div>");
    client.println("    <script>");
    client.println("        let ws = null;");
    client.println("        ");
    client.println("        function connectWS() {");
    client.println("            const wsUrl = 'ws://' + window.location.hostname + ':81/api/web_socket';");
    client.println("            ws = new WebSocket(wsUrl);");
    client.println("            ");
    client.println("            ws.onopen = function() {");
    client.println("                document.getElementById('status').className = 'status connected';");
    client.println("                document.getElementById('status').textContent = 'Connected';");
    client.println("                addMessage('System: Connected to WebSocket');");
    client.println("            };");
    client.println("            ");
    client.println("            ws.onclose = function() {");
    client.println("                document.getElementById('status').className = 'status disconnected';");
    client.println("                document.getElementById('status').textContent = 'Disconnected';");
    client.println("                addMessage('System: Disconnected');");
    client.println("            };");
    client.println("            ");
    client.println("            ws.onmessage = function(event) {");
    client.println("                addMessage('Server: ' + event.data);");
    client.println("            };");
    client.println("            ");
    client.println("            ws.onerror = function(error) {");
    client.println("                addMessage('Error: ' + error);");
    client.println("            };");
    client.println("        }");
    client.println("        ");
    client.println("        function disconnectWS() {");
    client.println("            if (ws) {");
    client.println("                ws.close();");
    client.println("                ws = null;");
    client.println("            }");
    client.println("        }");
    client.println("        ");
    client.println("        function sendMessage() {");
    client.println("            if (ws && ws.readyState === WebSocket.OPEN) {");
    client.println("                const message = document.getElementById('messageInput').value;");
    client.println("                ws.send(message);");
    client.println("                addMessage('You: ' + message);");
    client.println("                document.getElementById('messageInput').value = '';");
    client.println("            } else {");
    client.println("                addMessage('Error: Not connected');");
    client.println("            }");
    client.println("        }");
    client.println("        ");
    client.println("        function addMessage(message) {");
    client.println("            const messages = document.getElementById('messages');");
    client.println("            messages.innerHTML += '<div>' + message + '</div>';");
    client.println("            messages.scrollTop = messages.scrollHeight;");
    client.println("        }");
    client.println("        ");
    client.println("        // Auto connect");
    client.println("        connectWS();");
    client.println("    </script>");
    client.println("</body>");
    client.println("</html>");
    
    client.stop();
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

void WiFiManager::handleRoot() {
  WiFiClient client = server.client();
  
  // Отправляем HTTP заголовок
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  // Отправляем HTML частями
  client.println("<!DOCTYPE html>");
  client.println("<html>");
  client.println("<head>");
  client.println("    <title>ESP8266 Configuration</title>");
  client.println("    <meta charset=\"UTF-8\">");
  client.println("    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
  client.println("    <style>");
  client.println("        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }");
  client.println("        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }");
  client.println("        .tab { overflow: hidden; border: 1px solid #ccc; background-color: #f1f1f1; border-radius: 5px 5px 0 0; }");
  client.println("        .tab button { background-color: inherit; float: left; border: none; outline: none; cursor: pointer; padding: 14px 16px; transition: 0.3s; font-size: 17px; }");
  client.println("        .tab button:hover { background-color: #ddd; }");
  client.println("        .tab button.active { background-color: #4CAF50; color: white; }");
  client.println("        .tabcontent { display: none; padding: 20px; border: 1px solid #ccc; border-top: none; border-radius: 0 0 5px 5px; }");
  client.println("        .form-group { margin-bottom: 15px; }");
  client.println("        label { display: block; margin-bottom: 5px; font-weight: bold; }");
  client.println("        input, select, textarea { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }");
  client.println("        button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; margin: 5px; }");
  client.println("        button:hover { background-color: #45a049; }");
  client.println("        table { width: 100%; border-collapse: collapse; margin-top: 10px; }");
  client.println("        th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }");
  client.println("        th { background-color: #f2f2f2; }");
  client.println("        .modal { display: none; position: fixed; z-index: 1; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.4); }");
  client.println("        .modal-content { background-color: #fefefe; margin: 15% auto; padding: 20px; border: 1px solid #888; width: 80%; max-width: 500px; border-radius: 5px; }");
  client.println("        .close { color: #aaa; float: right; font-size: 28px; font-weight: bold; cursor: pointer; }");
  client.println("        .close:hover { color: black; }");
  client.println("        .wifi-network { border: 1px solid #ddd; padding: 10px; margin: 5px 0; border-radius: 4px; }");
  client.println("        .wifi-connected { background-color: #e8f5e8; }");
  client.println("        .wifi-disconnected { background-color: #f5f5f5; }");
  client.println("        .tablinks{ color: black; }");
  client.println("        .connection-status { padding: 10px; border-radius: 4px; margin: 10px 0; }");
  client.println("        .connected { background-color: #d4edda; color: #155724; }");
  client.println("        .disconnected { background-color: #f8d7da; color: #721c24; }");
  client.println("    </style>");
  
  // WebSocket JavaScript
  client.println("    <script>");
  client.println("        // WebSocket функции для основной страницы");
  client.println("        let mainWS = null;");
  client.println("        ");
  client.println("        function connectMainWebSocket() {");
  client.println("            const wsUrl = 'ws://' + window.location.hostname + ':81/api/web_socket';");
  client.println("            mainWS = new WebSocket(wsUrl);");
  client.println("            ");
  client.println("            mainWS.onopen = function() {");
  client.println("                console.log('Main WebSocket connected');");
  client.println("                updateWebSocketStatus('connected');");
  client.println("            };");
  client.println("            ");
  client.println("            mainWS.onclose = function() {");
  client.println("                console.log('Main WebSocket disconnected');");
  client.println("                updateWebSocketStatus('disconnected');");
  client.println("                // Попытка переподключения через 3 секунды");
  client.println("                setTimeout(connectMainWebSocket, 3000);");
  client.println("            };");
  client.println("            ");
  client.println("            mainWS.onmessage = function(event) {");
  client.println("                console.log('WebSocket message:', event.data);");
  client.println("                handleWebSocketMessage(event.data);");
  client.println("            };");
  client.println("            ");
  client.println("            mainWS.onerror = function(error) {");
  client.println("                console.error('WebSocket error:', error);");
  client.println("                updateWebSocketStatus('error');");
  client.println("            };");
  client.println("        }");
  client.println("        ");
  client.println("        function updateWebSocketStatus(status) {");
  client.println("            const statusElement = document.getElementById('websocketStatus');");
  client.println("            if (statusElement) {");
  client.println("                statusElement.textContent = 'WebSocket: ' + status;");
  client.println("                statusElement.className = 'connection-status ' + ");
  client.println("                    (status === 'connected' ? 'connected' : 'disconnected');");
  client.println("            }");
  client.println("        }");
  client.println("        ");
  client.println("        function handleWebSocketMessage(message) {");
  client.println("            // Обработка входящих сообщений WebSocket");
  client.println("            // Можно добавить свою логику здесь");
  client.println("            if (message.startsWith('DEVICE_UPDATE')) {");
  client.println("                loadConnectedDevices();");
  client.println("            }");
  client.println("        }");
  client.println("        ");
  client.println("        function sendWebSocketMessage(message) {");
  client.println("            if (mainWS && mainWS.readyState === WebSocket.OPEN) {");
  client.println("                mainWS.send(message);");
  client.println("            } else {");
  client.println("                console.warn('WebSocket not connected');");
  client.println("            }");
  client.println("        }");
  client.println("        ");
  client.println("        // Автоподключение при загрузке страницы");
  client.println("        document.addEventListener('DOMContentLoaded', function() {");
  client.println("            connectMainWebSocket();");
  client.println("            ");
  client.println("            // Добавляем статус WebSocket в интерфейс");
  client.println("            const statusTab = document.getElementById('Status');");
  client.println("            if (statusTab) {");
  client.println("                const statusElement = document.createElement('div');");
  client.println("                statusElement.id = 'websocketStatus';");
  client.println("                statusElement.className = 'connection-status disconnected';");
  client.println("                statusElement.textContent = 'WebSocket: disconnected';");
  client.println("                statusTab.insertBefore(statusElement, statusTab.firstChild);");
  client.println("            }");
  client.println("        });");
  client.println("    </script>");
  
  // Основной JavaScript
  client.println("    <script>");
  client.println("        function openTab(evt, tabName) {");
  client.println("            var i, tabcontent, tablinks;");
  client.println("            tabcontent = document.getElementsByClassName(\"tabcontent\");");
  client.println("            for (i = 0; i < tabcontent.length; i++) {");
  client.println("                tabcontent[i].style.display = \"none\";");
  client.println("            }");
  client.println("            tablinks = document.getElementsByClassName(\"tablinks\");");
  client.println("            for (i = 0; i < tablinks.length; i++) {");
  client.println("                tablinks[i].className = tablinks[i].className.replace(\" active\", \"\");");
  client.println("            }");
  client.println("            document.getElementById(tabName).style.display = \"block\";");
  client.println("            evt.currentTarget.className += \" active\";");
  client.println("            ");
  client.println("            if (tabName === 'Status') {");
  client.println("                loadConnectedDevices();");
  client.println("            } else if (tabName === 'APSettings') {");
  client.println("                loadAPSettings();");
  client.println("            } else if (tabName === 'WiFiScan') {");
  client.println("                loadWiFiStatus();");
  client.println("                scanWiFi();");
  client.println("            } else if (tabName === 'DeviceConfig') {");
  client.println("                loadDeviceConfig();");
  client.println("            } else if (tabName === 'DeviceControl') {");
  client.println("                loadDeviceControl();");
  client.println("            }");
  client.println("        }");
  client.println("");
  client.println("        function loadConnectedDevices() {");
  client.println("            fetch('/api/connected-devices')");
  client.println("                .then(response => response.json())");
  client.println("                .then(data => {");
  client.println("                    let table = '<table><tr><th>IP Адрес</th><th>MAC Адрес</th><th>Имя устройства</th><th>Комментарий</th><th>Действия</th></tr>';");
  client.println("                    data.devices.forEach(device => {");
  client.println("                        table += `<tr>");
  client.println("                            <td>${device.ip}</td>");
  client.println("                            <td>${device.mac}</td>");
  client.println("                            <td>${device.device_name || ''}</td>");
  client.println("                            <td>${device.device_comment || ''}</td>");
  client.println("                            <td><button onclick=\"showDeviceInfo('${device.mac}', '${device.device_name || ''}', '${device.device_comment || ''}')\">Информация об устройстве</button></td>");
  client.println("                        </tr>`;");
  client.println("                    });");
  client.println("                    table += '</table>';");
  client.println("                    document.getElementById('connectedDevicesTable').innerHTML = table;");
  client.println("                });");
  client.println("        }");
  client.println("");
  client.println("        function showDeviceInfo(mac, name, comment) {");
  client.println("            document.getElementById('modalDeviceMac').value = mac;");
  client.println("            document.getElementById('modalDeviceName').value = name || '';");
  client.println("            document.getElementById('modalDeviceComment').value = comment || '';");
  client.println("            document.getElementById('deviceInfoModal').style.display = 'block';");
  client.println("        }");
  client.println("");
  client.println("        function closeModal() {");
  client.println("            document.getElementById('deviceInfoModal').style.display = 'none';");
  client.println("        }");
  client.println("");
  client.println("        function saveDeviceInfo() {");
  client.println("            const mac = document.getElementById('modalDeviceMac').value;");
  client.println("            const name = document.getElementById('modalDeviceName').value;");
  client.println("            const comment = document.getElementById('modalDeviceComment').value;");
  client.println("            ");
  client.println("            fetch('/api/device-info', {");
  client.println("                method: 'POST',");
  client.println("                headers: { 'Content-Type': 'application/json' },");
  client.println("                body: JSON.stringify({ mac: mac, device_name: name, device_comment: comment })");
  client.println("            }).then(() => {");
  client.println("                closeModal();");
  client.println("                loadConnectedDevices();");
  client.println("            });");
  client.println("        }");
  client.println("");
  client.println("        function loadAPSettings() {");
  client.println("            fetch('/api/settings')");
  client.println("                .then(response => response.json())");
  client.println("                .then(settings => {");
  client.println("                    document.getElementById('apSsid').value = settings.ap_ssid || '';");
  client.println("                    document.getElementById('apPassword').value = settings.ap_password || '';");
  client.println("                    ");
  client.println("                    const subnetSelect = document.getElementById('subnet');");
  client.println("                    subnetSelect.innerHTML = '';");
  client.println("                    for (let i = 1; i <= 255; i++) {");
  client.println("                        const option = document.createElement('option');");
  client.println("                        option.value = i;");
  client.println("                        option.textContent = '192.168.' + i + '.1';");
  client.println("                        if (i === (settings.subnet || 4)) {");
  client.println("                            option.selected = true;");
  client.println("                        }");
  client.println("                        subnetSelect.appendChild(option);");
  client.println("                    }");
  client.println("                });");
  client.println("        }");
  client.println("");
  client.println("        function saveAPSettings() {");
  client.println("            const settings = {");
  client.println("                ap_ssid: document.getElementById('apSsid').value,");
  client.println("                ap_password: document.getElementById('apPassword').value,");
  client.println("                subnet: parseInt(document.getElementById('subnet').value)");
  client.println("            };");
  client.println("            ");
  client.println("            fetch('/api/settings', {");
  client.println("                method: 'POST',");
  client.println("                headers: { 'Content-Type': 'application/json' },");
  client.println("                body: JSON.stringify(settings)");
  client.println("            }).then(() => alert('Настройки точки доступа сохранены'));");
  client.println("        }");
  client.println("");
  client.println("        function scanWiFi() {");
  client.println("            fetch('/api/wifi-scan')");
  client.println("                .then(response => response.json())");
  client.println("                .then(data => {");
  client.println("                    let networksHTML = '<h3>Доступные сети:</h3>';");
  client.println("                    data.networks.forEach(network => {");
  client.println("                        const encryption = network.encryption === 7 ? 'Open' : 'Secured';");
  client.println("                        networksHTML += `");
  client.println("                            <div class=\"wifi-network\">");
  client.println("                                <strong>${network.ssid}</strong><br>");
  client.println("                                Сигнал: ${network.rssi}dBm | Защита: ${encryption}<br>");
  client.println("                                <input type=\"password\" id=\"password_${network.ssid.replace(/[^a-zA-Z0-9]/g, '_')}\" placeholder=\"Пароль\" style=\"width: 200px; margin: 5px 0;\">");
  client.println("                                <button onclick=\"connectToNetwork('${network.ssid}', ${network.encryption})\">Подключиться</button>");
  client.println("                            </div>");
  client.println("                        `;");
  client.println("                    });");
  client.println("                    document.getElementById('wifiNetworks').innerHTML = networksHTML;");
  client.println("                });");
  client.println("        }");
  client.println("");
  client.println("        function connectToNetwork(ssid, encryption) {");
  client.println("            const passwordId = 'password_' + ssid.replace(/[^a-zA-Z0-9]/g, '_');");
  client.println("            const password = document.getElementById(passwordId).value;");
  client.println("            ");
  client.println("            if (encryption !== 7 && !password) {");
  client.println("                alert('Для защищенной сети необходим пароль');");
  client.println("                return;");
  client.println("            }");
  client.println("            ");
  client.println("            const connectionData = {");
  client.println("                ssid: ssid,");
  client.println("                password: password");
  client.println("            };");
  client.println("            ");
  client.println("            fetch('/api/wifi-connect', {");
  client.println("                method: 'POST',");
  client.println("                headers: { 'Content-Type': 'application/json' },");
  client.println("                body: JSON.stringify(connectionData)");
  client.println("            })");
  client.println("            .then(response => response.json())");
  client.println("            .then(data => {");
  client.println("                alert('Подключение к сети ' + ssid + ' выполнено');");
  client.println("                loadWiFiStatus();");
  client.println("            })");
  client.println("            .catch(error => {");
  client.println("                alert('Ошибка подключения к сети');");
  client.println("            });");
  client.println("        }");
  client.println("");
  client.println("        function disconnectFromWiFi() {");
  client.println("            if (confirm('Отключиться от текущей WiFi сети?')) {");
  client.println("                fetch('/api/wifi-disconnect', { method: 'POST' })");
  client.println("                    .then(response => response.json())");
  client.println("                    .then(data => {");
  client.println("                        alert('Отключено от WiFi сети');");
  client.println("                        loadWiFiStatus();");
  client.println("                        scanWiFi();");
  client.println("                    });");
  client.println("            }");
  client.println("        }");
  client.println("");
  client.println("        function loadWiFiStatus() {");
  client.println("            fetch('/api/wifi-status')");
  client.println("                .then(response => response.json())");
  client.println("                .then(data => {");
  client.println("                    let statusHTML = '';");
  client.println("                    if (data.connected) {");
  client.println("                        statusHTML = `");
  client.println("                            <div class=\"connection-status connected\">");
  client.println("                                <strong>Подключено к WiFi</strong><br>");
  client.println("                                Сеть: ${data.ssid}<br>");
  client.println("                                IP: ${data.ip}<br>");
  client.println("                                Сигнал: ${data.rssi}dBm");
  client.println("                            </div>");
  client.println("                            <button onclick=\"disconnectFromWiFi()\" style=\"background-color: #f44336;\">Отключиться от точки доступа</button>");
  client.println("                        `;");
  client.println("                    } else {");
  client.println("                        statusHTML = `");
  client.println("                            <div class=\"connection-status disconnected\">");
  client.println("                                <strong>Не подключено к WiFi</strong>");
  client.println("                            </div>");
  client.println("                        `;");
  client.println("                    }");
  client.println("                    document.getElementById('wifiStatus').innerHTML = statusHTML;");
  client.println("                });");
  client.println("        }");
  client.println("");
  client.println("        function loadDeviceConfig() {");
  client.println("            fetch('/api/settings')");
  client.println("                .then(response => response.json())");
  client.println("                .then(settings => {");
  client.println("                    document.getElementById('deviceName').value = settings.device_name || '';");
  client.println("                    document.getElementById('deviceComment').value = settings.device_comment || '';");
  client.println("                });");
  client.println("        }");
  client.println("");
  client.println("        function saveDeviceConfig() {");
  client.println("            const config = {");
  client.println("                device_name: document.getElementById('deviceName').value,");
  client.println("                device_comment: document.getElementById('deviceComment').value");
  client.println("            };");
  client.println("            ");
  client.println("            fetch('/api/settings', {");
  client.println("                method: 'POST',");
  client.println("                headers: { 'Content-Type': 'application/json' },");
  client.println("                body: JSON.stringify(config)");
  client.println("            }).then(() => alert('Настройки устройства сохранены'));");
  client.println("        }");
  client.println("");
  client.println("        function loadDeviceControl() {");
  client.println("            fetch('/api/settings')");
  client.println("                .then(response => response.json())");
  client.println("                .then(settings => {");
  client.println("                    document.getElementById('apModeEnabled').checked = settings.ap_mode_enabled || false;");
  client.println("                    document.getElementById('clientModeEnabled').checked = settings.client_mode_enabled || false;");
  client.println("                });");
  client.println("        }");
  client.println("");
  client.println("        function saveDeviceControl() {");
  client.println("            const control = {");
  client.println("                ap_mode_enabled: document.getElementById('apModeEnabled').checked,");
  client.println("                client_mode_enabled: document.getElementById('clientModeEnabled').checked");
  client.println("            };");
  client.println("            ");
  client.println("            fetch('/api/settings', {");
  client.println("                method: 'POST',");
  client.println("                headers: { 'Content-Type': 'application/json' },");
  client.println("                body: JSON.stringify(control)");
  client.println("            }).then(() => alert('Настройки управления сохранены'));");
  client.println("        }");
  client.println("");
  client.println("        function clearSettings() {");
  client.println("            if (confirm('Вы уверены, что хотите очистить все настройки?')) {");
  client.println("                fetch('/api/clear-settings', { method: 'POST' })");
  client.println("                    .then(() => alert('Настройки очищены'));");
  client.println("            }");
  client.println("        }");
  client.println("");
  client.println("        function restartDevice() {");
  client.println("            if (confirm('Перезагрузить устройство?')) {");
  client.println("                fetch('/api/restart', { method: 'POST' });");
  client.println("            }");
  client.println("        }");
  client.println("");
  client.println("        document.addEventListener('DOMContentLoaded', function() {");
  client.println("            loadConnectedDevices();");
  client.println("            loadAPSettings();");
  client.println("        });");
  client.println("    </script>");
  client.println("</head>");
  client.println("<body>");
  client.println("    <div class=\"container\">");
  client.println("        <h1>ESP8266 Configuration</h1>");
  client.println("        ");
  client.println("        <div class=\"tab\">");
  client.println("            <button class=\"tablinks active\" onclick=\"openTab(event, 'Status')\">Статус</button>");
  client.println("            <button class=\"tablinks\" onclick=\"openTab(event, 'APSettings')\">Настройки точки доступа</button>");
  client.println("            <button class=\"tablinks\" onclick=\"openTab(event, 'WiFiScan')\">Сканирование WiFi сетей</button>");
  client.println("            <button class=\"tablinks\" onclick=\"openTab(event, 'DeviceConfig')\">Настройки устройства</button>");
  client.println("            <button class=\"tablinks\" onclick=\"openTab(event, 'DeviceControl')\">Управление устройством</button>");
  client.println("        </div>");
  
  // Status Tab
  client.println("        <div id=\"Status\" class=\"tabcontent\" style=\"display: block;\">");
  client.println("            <h2>Список подключенных устройств</h2>");
  client.println("            <div id=\"connectedDevicesTable\"></div>");
  client.println("        </div>");
  
  // APSettings Tab
  client.println("        <div id=\"APSettings\" class=\"tabcontent\">");
  client.println("            <h2>Настройки точки доступа</h2>");
  client.println("            <div class=\"form-group\">");
  client.println("                <label for=\"apSsid\">Имя точки доступа (SSID):</label>");
  client.println("                <input type=\"text\" id=\"apSsid\">");
  client.println("            </div>");
  client.println("            <div class=\"form-group\">");
  client.println("                <label for=\"apPassword\">Пароль точки доступа:</label>");
  client.println("                <input type=\"password\" id=\"apPassword\">");
  client.println("            </div>");
  client.println("            <div class=\"form-group\">");
  client.println("                <label for=\"subnet\">Подсеть (192.168.XXX.1):</label>");
  client.println("                <select id=\"subnet\"></select>");
  client.println("            </div>");
  client.println("            <button onclick=\"saveAPSettings()\">Сохранить настройки AP</button>");
  client.println("        </div>");
  
  // WiFiScan Tab
  client.println("        <div id=\"WiFiScan\" class=\"tabcontent\">");
  client.println("            <h2>Сканирование WiFi сетей</h2>");
  client.println("            <div id=\"wifiStatus\"></div>");
  client.println("            <button onclick=\"scanWiFi()\">Сканировать сети</button>");
  client.println("            <button onclick=\"loadWiFiStatus()\">Обновить статус</button>");
  client.println("            <div id=\"wifiNetworks\"></div>");
  client.println("        </div>");
  
  // DeviceConfig Tab
  client.println("        <div id=\"DeviceConfig\" class=\"tabcontent\">");
  client.println("            <h2>Настройки устройства</h2>");
  client.println("            <div class=\"form-group\">");
  client.println("                <label for=\"deviceName\">Имя устройства:</label>");
  client.println("                <input type=\"text\" id=\"deviceName\">");
  client.println("            </div>");
  client.println("            <div class=\"form-group\">");
  client.println("                <label for=\"deviceComment\">Комментарий для устройства:</label>");
  client.println("                <textarea id=\"deviceComment\"></textarea>");
  client.println("            </div>");
  client.println("            <button onclick=\"saveDeviceConfig()\">Сохранить настройки устройства</button>");
  client.println("        </div>");
  
  // DeviceControl Tab
  client.println("        <div id=\"DeviceControl\" class=\"tabcontent\">");
  client.println("            <h2>Управление устройством</h2>");
  client.println("            <div class=\"form-group\">");
  client.println("                <label>");
  client.println("                    <input type=\"checkbox\" id=\"apModeEnabled\">");
  client.println("                    Режим точки доступа");
  client.println("                </label>");
  client.println("            </div>");
  client.println("            <div class=\"form-group\">");
  client.println("                <label>");
  client.println("                    <input type=\"checkbox\" id=\"clientModeEnabled\">");
  client.println("                    Режим клиента WiFi");
  client.println("                </label>");
  client.println("            </div>");
  client.println("            <button onclick=\"saveDeviceControl()\">Сохранить настройки</button>");
  client.println("            <button onclick=\"clearSettings()\" style=\"background-color: #f44336;\">Очистить настройки</button>");
  client.println("            <button onclick=\"restartDevice()\" style=\"background-color: #ff9800;\">Перезагрузить устройство</button>");
  client.println("        </div>");
  
  // Modal Window
  client.println("        <!-- Модальное окно для информации об устройстве -->");
  client.println("        <div id=\"deviceInfoModal\" class=\"modal\">");
  client.println("            <div class=\"modal-content\">");
  client.println("                <span class=\"close\" onclick=\"closeModal()\">&times;</span>");
  client.println("                <h2>Информация об устройстве</h2>");
  client.println("                <div class=\"form-group\">");
  client.println("                    <label for=\"modalDeviceName\">Имя устройства:</label>");
  client.println("                    <input type=\"text\" id=\"modalDeviceName\">");
  client.println("                </div>");
  client.println("                <div class=\"form-group\">");
  client.println("                    <label for=\"modalDeviceComment\">Комментарий:</label>");
  client.println("                    <textarea id=\"modalDeviceComment\"></textarea>");
  client.println("                </div>");
  client.println("                <input type=\"hidden\" id=\"modalDeviceMac\">");
  client.println("                <button onclick=\"saveDeviceInfo()\">Сохранить</button>");
  client.println("            </div>");
  client.println("        </div>");
  client.println("    </div>");
  client.println("</body>");
  client.println("</html>");
  
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
