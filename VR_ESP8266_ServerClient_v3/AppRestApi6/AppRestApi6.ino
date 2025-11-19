#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

ESP8266WebServer server(80);

// Структура для хранения информации о клиентах
struct ClientInfo {
  String ip;
  String mac;
  String deviceName;
  String comment;
  bool sensorDevice;
};

// Структура для хранения данных в EEPROM
struct ConfigData {
  char ssid[32];
  char password[64];
  uint8_t subnet;
};

// Структура для сохранения данных клиентов
struct ClientData {
  char mac[18];
  char deviceName[32];
  char comment[64];
  bool sensorDevice;
};

// Хранилище клиентов
ClientInfo clients[10];
int clientCount = 0;
String apSSID = "MyApp";
String apPassword = "12345678";

IPAddress apIP(192, 168, 4, 1);
IPAddress apGateway(192, 168, 4, 1);
IPAddress apSubnet(255, 255, 255, 0);

// Адреса в EEPROM
#define EEPROM_SIZE 1024  // Увеличили для хранения данных клиентов
#define CONFIG_START 0
#define CLIENTS_DATA_START 100  // Начало хранения данных клиентов
#define MAX_CLIENTS 10

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Starting ESP8266 AP...");
  
  // Инициализация EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Загрузка конфигурации из EEPROM
  loadConfig();
  
  // Запуск точки доступа
  setupAP();
  
  // Настройка REST API
  setupAPI();
  
  // Запуск сервера
  server.begin();
  Serial.println("HTTP server started");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void loadConfig() {
  Serial.println("Loading config from EEPROM...");
  
  ConfigData config;
  EEPROM.get(CONFIG_START, config);
  
  // Проверка валидности данных (первый запуск)
  if (config.subnet >= 1 && config.subnet <= 254) {
    apSSID = String(config.ssid);
    if (apSSID.length() == 0) {
      apSSID = "ESP8266-AP";
    }
    
    apPassword = String(config.password);
    if (apPassword.length() == 0) {
      apPassword = "12345678";
    }
    
    // Восстановление подсети
    apIP = IPAddress(192, 168, config.subnet, 1);
    apGateway = IPAddress(192, 168, config.subnet, 1);
    
    Serial.println("Config loaded from EEPROM");
    Serial.print("Subnet: 192.168.");
    Serial.print(config.subnet);
    Serial.println(".1");
  } else {
    // Значения по умолчанию
    Serial.println("Using default config");
    saveConfig();
  }
}

void saveConfig() {
  Serial.println("Saving config to EEPROM...");
  
  ConfigData config;
  
  // Копируем текущие настройки
  strncpy(config.ssid, apSSID.c_str(), sizeof(config.ssid) - 1);
  config.ssid[sizeof(config.ssid) - 1] = '\0';
  
  strncpy(config.password, apPassword.c_str(), sizeof(config.password) - 1);
  config.password[sizeof(config.password) - 1] = '\0';
  
  config.subnet = apIP[2]; // Третий октет IP (192.168.X.1)
  
  // Сохраняем в EEPROM
  EEPROM.put(CONFIG_START, config);
  
  if (EEPROM.commit()) {
    Serial.println("Config saved to EEPROM");
  } else {
    Serial.println("EEPROM commit failed!");
  }
}

// Сохранение данных клиента
void saveClientData(const String &mac, const String &deviceName, const String &comment, bool sensorDevice) {
  Serial.println("Saving client data for MAC: " + mac);
  
  ClientData clientData;
  
  // Копируем данные
  strncpy(clientData.mac, mac.c_str(), sizeof(clientData.mac) - 1);
  clientData.mac[sizeof(clientData.mac) - 1] = '\0';
  
  strncpy(clientData.deviceName, deviceName.c_str(), sizeof(clientData.deviceName) - 1);
  clientData.deviceName[sizeof(clientData.deviceName) - 1] = '\0';
  
  strncpy(clientData.comment, comment.c_str(), sizeof(clientData.comment) - 1);
  clientData.comment[sizeof(clientData.comment) - 1] = '\0';
  
  clientData.sensorDevice = sensorDevice;
  
  // Сохраняем в EEPROM
  int address = CLIENTS_DATA_START;
  bool found = false;
  
  for (int i = 0; i < MAX_CLIENTS; i++) {
    ClientData storedData;
    EEPROM.get(address, storedData);
    
    // Если нашли существующую запись или пустую ячейку
    if (strlen(storedData.mac) == 0 || String(storedData.mac) == mac) {
      EEPROM.put(address, clientData);
      found = true;
      break;
    }
    address += sizeof(ClientData);
  }
  
  if (!found) {
    // Перезаписываем первую запись (самую старую)
    EEPROM.put(CLIENTS_DATA_START, clientData);
  }
  
  if (EEPROM.commit()) {
    Serial.println("Client data saved to EEPROM");
  } else {
    Serial.println("EEPROM commit failed for client data!");
  }
}

// Загрузка данных клиента по MAC
void loadClientData(const String &mac, String &deviceName, String &comment, bool &sensorDevice) {
  int address = CLIENTS_DATA_START;
  
  for (int i = 0; i < MAX_CLIENTS; i++) {
    ClientData clientData;
    EEPROM.get(address, clientData);
    
    if (String(clientData.mac) == mac) {
      deviceName = String(clientData.deviceName);
      comment = String(clientData.comment);
      sensorDevice = clientData.sensorDevice;
      Serial.println("Loaded client data for MAC: " + mac);
      return;
    }
    address += sizeof(ClientData);
  }
  
  // Если данные не найдены
  deviceName = "";
  comment = "";
  sensorDevice = false;
}

void setupAP() {
  Serial.println("Setting up Access Point...");
  
  WiFi.mode(WIFI_AP);
  delay(100);
  
  if (!WiFi.softAPConfig(apIP, apGateway, apSubnet)) {
    Serial.println("AP Config failed!");
  }
  delay(100);
  
  if (!WiFi.softAP(apSSID.c_str(), apPassword.c_str())) {
    Serial.println("AP Start failed!");
  }
  delay(100);
  
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}

String macToString(uint8_t* mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

// Экранирование кавычек и переводов строк для JavaScript
String escapeJS(const String &input) {
  String output = "";
  for (unsigned int i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    if (c == '"') {
      output += "\\\"";
    } else if (c == '\n') {
      output += "\\n";
    } else if (c == '\r') {
      output += "\\r";
    } else if (c == '\t') {
      output += "\\t";
    } else if (c == '\'') {
      output += "\\'";
    } else {
      output += c;
    }
  }
  return output;
}

void updateClientList() {
  // Используем правильный API для получения списка клиентов
  struct station_info *station_list = wifi_softap_get_station_info();
  struct station_info *station = station_list;
  
  clientCount = 0;
  
  while (station != NULL && clientCount < 10) {
    String clientMac = macToString(station->bssid);
    clients[clientCount].ip = IPAddress(station->ip).toString();
    clients[clientCount].mac = clientMac;
    
    // Загружаем сохраненные данные из EEPROM
    String savedDeviceName, savedComment;
    bool savedSensorDevice;
    loadClientData(clientMac, savedDeviceName, savedComment, savedSensorDevice);
    
    if (savedDeviceName.length() > 0) {
      clients[clientCount].deviceName = savedDeviceName;
    } else {
      // Если имя устройства не установлено, генерируем его
      clients[clientCount].deviceName = "Device_" + clientMac.substring(12);
    }
    
    clients[clientCount].comment = savedComment;
    clients[clientCount].sensorDevice = savedSensorDevice;
    
    // Переходим к следующей станции
    station = STAILQ_NEXT(station, next);
    clientCount++;
  }
  
  // Освобождаем память
  wifi_softap_free_station_info();
  
  Serial.print("Found ");
  Serial.print(clientCount);
  Serial.println(" connected clients");
}

void setupAPI() {
  // Главная страница
  server.on("/", HTTP_GET, []() {
    WiFiClient client = server.client();
    
    // Быстрая отправка заголовков
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println("Access-Control-Allow-Origin: *");
    client.println();
    
    // Отправка HTML построчно
    client.println("<!DOCTYPE html>");
    client.println("<html>");
    client.println("<head>");
    client.println("<meta charset='UTF-8'>");
    client.println("<title>ESP8266 AP Configuration</title>");
    client.println("<style>");
    client.println("body { font-family: Arial; margin: 20px; background: #f5f5f5; }");
    client.println(".container { max-width: 1400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; }");
    client.println(".section { margin-bottom: 20px; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }");
    client.println("table { width: 100%; border-collapse: collapse; margin-top: 10px; }");
    client.println("th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }");
    client.println("th { background-color: #f2f2f2; }");
    client.println("button { padding: 5px 10px; margin: 2px; cursor: pointer; background: #4CAF50; color: white; border: none; border-radius: 3px; }");
    client.println("button:hover { background: #45a049; }");
    client.println(".btn-blue { background: #2196F3; }");
    client.println(".btn-blue:hover { background: #0b7dda; }");
    client.println(".btn-orange { background: #ff9800; }");
    client.println(".btn-orange:hover { background: #e68a00; }");
    client.println(".modal { display: none; position: fixed; z-index: 1; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.4); }");
    client.println(".modal-content { background: #fefefe; margin: 15% auto; padding: 20px; border: 1px solid #888; width: 50%; border-radius: 5px; }");
    client.println(".close { color: #aaa; float: right; font-size: 28px; font-weight: bold; cursor: pointer; }");
    client.println("select, input, textarea { padding: 5px; margin: 5px; width: 200px; }");
    client.println(".ip-link { color: #2196F3; text-decoration: underline; cursor: pointer; }");
    client.println(".ip-link:hover { color: #0b7dda; }");
    client.println(".checkbox-container { margin: 10px 0; }");
    client.println(".checkbox-container input { width: auto; }");
    client.println("</style>");
    client.println("</head>");
    client.println("<body>");
    client.println("<div class='container'>");
    client.println("<h1>ESP8266 Access Point</h1>");
    
    // Секция REST API
    client.println("<div class='section'>");
    client.println("<h2>REST API</h2>");
    client.println("<button class='btn-blue' onclick=\"window.open('/api/sensorDevices', '_blank')\">Open Sensor Devices API</button>");
    client.println("<p>Get list of devices with SensorDevice=true in JSON format</p>");
    client.println("</div>");
    
    // Секция настройки подсети
    client.println("<div class='section'>");
    client.println("<h2>Network Settings</h2>");
    client.println("<label>Subnet: 192.168.</label>");
    client.println("<select id='subnetSelect'>");
    
    // Только основные опции для экономии памяти
    int currentSubnet = apIP[2];
    for (int i = 1; i < 255; i++) {
      if (i == currentSubnet) {
        client.println("<option value='" + String(i) + "' selected>" + String(i) + "</option>");
      } else {
        client.println("<option value='" + String(i) + "'>" + String(i) + "</option>");
      }
    }
    
    client.println("</select>");
    client.println("<label>.1</label>");
    client.println("<button onclick='changeSubnet()'>Apply</button>");
    client.println("<p>Current AP IP: <span id='currentIP'>" + apIP.toString() + "</span></p>");
    client.println("</div>");
    
    // Секция подключенных устройств
    client.println("<div class='section'>");
    client.println("<h2>Connected Devices</h2>");
    client.println("<button onclick='loadClients()'>Refresh</button>");
    client.println("<table>");
    client.println("<thead>");
    client.println("<tr><th>IP Address</th><th>MAC Address</th><th>Device Name</th><th>Comment</th><th>Sensor Device</th><th>Actions</th></tr>");
    client.println("</thead>");
    client.println("<tbody id='clientsBody'>");
    client.println("</tbody>");
    client.println("</table>");
    client.println("</div>");
    
    // Модальное окно
    client.println("<div id='editModal' class='modal'>");
    client.println("<div class='modal-content'>");
    client.println("<span class='close' onclick='closeModal()'>&times;</span>");
    client.println("<h3>Edit Device</h3>");
    client.println("<input type='hidden' id='editIp'>");
    client.println("<input type='hidden' id='editMac'>");
    client.println("<label>Device Name:</label><br>");
    client.println("<input type='text' id='editDeviceName' style='width: 100%'><br>");
    client.println("<label>Comment:</label><br>");
    client.println("<textarea id='editComment' rows='4' style='width: 100%'></textarea><br>");
    client.println("<div class='checkbox-container'>");
    client.println("<input type='checkbox' id='editSensorDevice'>");
    client.println("<label for='editSensorDevice'>Sensor Device</label>");
    client.println("</div>");
    client.println("<button onclick='saveDevice()'>Save</button>");
    client.println("<button onclick='closeModal()'>Cancel</button>");
    client.println("</div>");
    client.println("</div>");
    
    // JavaScript
    client.println("<script>");
    client.println("function loadClients() {");
    client.println("  fetch('/api/clients')");
    client.println("    .then(response => response.json())");
    client.println("    .then(data => {");
    client.println("      const tbody = document.getElementById('clientsBody');");
    client.println("      tbody.innerHTML = '';");
    client.println("      data.forEach(client => {");
    client.println("        const row = document.createElement('tr');");
    client.println("        row.innerHTML = `");
    client.println("          <td><span class='ip-link' onclick=\"window.open('http://' + '${client.ip}', '_blank')\">${client.ip}</span></td>");
    client.println("          <td>${client.mac}</td>");
    client.println("          <td>${client.deviceName}</td>");
    client.println("          <td>${client.comment}</td>");
    client.println("          <td>${client.sensorDevice ? 'Yes' : 'No'}</td>");
    client.println("          <td><button onclick=\"editDevice('${client.ip}', '${client.mac}', '${escapeJS(client.deviceName)}', '${escapeJS(client.comment)}', ${client.sensorDevice})\">Edit</button></td>");
    client.println("        `;");
    client.println("        tbody.appendChild(row);");
    client.println("      });");
    client.println("    });");
    client.println("}");
    
    client.println("function editDevice(ip, mac, deviceName, comment, sensorDevice) {");
    client.println("  document.getElementById('editIp').value = ip;");
    client.println("  document.getElementById('editMac').value = mac;");
    client.println("  document.getElementById('editDeviceName').value = deviceName;");
    client.println("  document.getElementById('editComment').value = comment;");
    client.println("  document.getElementById('editSensorDevice').checked = sensorDevice;");
    client.println("  document.getElementById('editModal').style.display = 'block';");
    client.println("}");
    
    client.println("function saveDevice() {");
    client.println("  const ip = document.getElementById('editIp').value;");
    client.println("  const mac = document.getElementById('editMac').value;");
    client.println("  const deviceName = document.getElementById('editDeviceName').value;");
    client.println("  const comment = document.getElementById('editComment').value;");
    client.println("  const sensorDevice = document.getElementById('editSensorDevice').checked;");
    client.println("  ");
    client.println("  fetch('/api/updateClient', {");
    client.println("    method: 'POST',");
    client.println("    headers: { 'Content-Type': 'application/json' },");
    client.println("    body: JSON.stringify({ ip, mac, deviceName, comment, sensorDevice })");
    client.println("  }).then(() => {");
    client.println("    closeModal();");
    client.println("    loadClients();");
    client.println("  });");
    client.println("}");
    
    client.println("function changeSubnet() {");
    client.println("  const subnet = document.getElementById('subnetSelect').value;");
    client.println("  fetch('/api/changeSubnet', {");
    client.println("    method: 'POST',");
    client.println("    headers: { 'Content-Type': 'application/json' },");
    client.println("    body: JSON.stringify({ subnet })");
    client.println("  }).then(response => response.json())");
    client.println("    .then(data => {");
    client.println("      if (data.status === 'success') {");
    client.println("        document.getElementById('currentIP').textContent = data.newIP;");
    client.println("        alert('Subnet changed to ' + data.newIP);");
    client.println("      }");
    client.println("    });");
    client.println("}");
    
    client.println("function closeModal() {");
    client.println("  document.getElementById('editModal').style.display = 'none';");
    client.println("}");
    
    client.println("// Escape function for JavaScript");
    client.println("function escapeJS(str) {");
    client.println("  return str.replace(/'/g, \"\\\\'\").replace(/\"/g, '\\\\\"').replace(/\\n/g, \"\\\\n\").replace(/\\r/g, \"\\\\r\");");
    client.println("}");
    
    client.println("// Load clients on page load");
    client.println("loadClients();");
    client.println("</script>");
    
    client.println("</div>");
    client.println("</body>");
    client.println("</html>");
    client.stop();
  });

  // Получить список клиентов
  server.on("/api/clients", HTTP_GET, []() {
    updateClientList();
    
    DynamicJsonDocument doc(2048);
    JsonArray array = doc.to<JsonArray>();
    
    for (int i = 0; i < clientCount; i++) {
      JsonObject obj = array.createNestedObject();
      obj["ip"] = clients[i].ip;
      obj["mac"] = clients[i].mac;
      obj["deviceName"] = clients[i].deviceName;
      obj["comment"] = clients[i].comment;
      obj["sensorDevice"] = clients[i].sensorDevice;
    }
    
    String response;
    serializeJson(doc, response);
    
    WiFiClient client = server.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println("Access-Control-Allow-Origin: *");
    client.println();
    client.print(response);
    client.stop();
  });

  // Получить список сенсорных устройств
  server.on("/api/sensorDevices", HTTP_GET, []() {
    updateClientList();
    
    DynamicJsonDocument doc(2048);
    JsonArray array = doc.to<JsonArray>();
    
    for (int i = 0; i < clientCount; i++) {
      if (clients[i].sensorDevice) {
        JsonObject obj = array.createNestedObject();
        obj["ip"] = clients[i].ip;
        obj["mac"] = clients[i].mac;
        obj["deviceName"] = clients[i].deviceName;
        obj["comment"] = clients[i].comment;
        obj["sensorDevice"] = clients[i].sensorDevice;
      }
    }
    
    String response;
    serializeJson(doc, response);
    
    WiFiClient client = server.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println("Access-Control-Allow-Origin: *");
    client.println();
    client.print(response);
    client.stop();
  });

  // Обновить информацию о клиенте
  server.on("/api/updateClient", HTTP_POST, []() {
    WiFiClient client = server.client();
    
    if (server.hasArg("plain")) {
      String body = server.arg("plain");
      DynamicJsonDocument doc(512);
      deserializeJson(doc, body);
      
      String ip = doc["ip"];
      String mac = doc["mac"];
      String deviceName = doc["deviceName"];
      String comment = doc["comment"];
      bool sensorDevice = doc["sensorDevice"];
      
      // Сохраняем данные в EEPROM по MAC-адресу
      saveClientData(mac, deviceName, comment, sensorDevice);
      
      // Также обновляем данные в текущем списке клиентов
      for (int i = 0; i < clientCount; i++) {
        if (clients[i].mac == mac) {
          clients[i].deviceName = deviceName;
          clients[i].comment = comment;
          clients[i].sensorDevice = sensorDevice;
          break;
        }
      }
      
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json");
      client.println("Connection: close");
      client.println("Access-Control-Allow-Origin: *");
      client.println();
      client.print("{\"status\":\"success\"}");
      client.stop();
    }
  });

  // Изменить подсеть
  server.on("/api/changeSubnet", HTTP_POST, []() {
    WiFiClient client = server.client();
    
    if (server.hasArg("plain")) {
      String body = server.arg("plain");
      DynamicJsonDocument doc(128);
      deserializeJson(doc, body);
      
      String subnet = doc["subnet"];
      int subnetValue = subnet.toInt();
      
      if (subnetValue >= 1 && subnetValue <= 254) {
        IPAddress newIP(192, 168, subnetValue, 1);
        IPAddress newGateway(192, 168, subnetValue, 1);
        
        WiFi.softAPConfig(newIP, newGateway, apSubnet);
        apIP = newIP;
        apGateway = newGateway;
        
        // Сохраняем новую конфигурацию
        saveConfig();
        
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println("Access-Control-Allow-Origin: *");
        client.println();
        client.print("{\"status\":\"success\",\"newIP\":\"" + newIP.toString() + "\"}");
        client.stop();
      } else {
        client.println("HTTP/1.1 400 Bad Request");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println("Access-Control-Allow-Origin: *");
        client.println();
        client.print("{\"status\":\"error\",\"message\":\"Invalid subnet\"}");
        client.stop();
      }
    }
  });

  // OPTIONS для CORS
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      WiFiClient client = server.client();
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println("Access-Control-Allow-Origin: *");
      client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
      client.println("Access-Control-Allow-Headers: Content-Type");
      client.println();
      client.stop();
    } else {
      WiFiClient client = server.client();
      client.println("HTTP/1.1 404 Not Found");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.print("Not Found");
      client.stop();
    }
  });
}

void loop() {
  server.handleClient();
  delay(1); // Даем время другим процессам
}
