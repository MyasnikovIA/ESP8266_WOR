#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// Конфигурация точки доступа
const char* ssid = "ESP8266_AP";
const char* password = "12345678";
IPAddress local_ip(192, 168, 5, 1);
IPAddress gateway(192, 168, 5, 1);
IPAddress subnet(255, 255, 255, 0);

ESP8266WebServer server(80);

// Структура для хранения информации о клиентах
struct ClientInfo {
  IPAddress ip;
  String mac;
  String hostname;
  String comment;
  bool isWebServer;
  bool isSensorDevice;
  unsigned long lastSeen; // Время последнего обнаружения
  bool connected; // Статус подключения
};

// Структура для сохранения в EEPROM
struct StoredClient {
  char mac[18];
  char hostname[32];
  char comment[64];
  bool isSensorDevice;
};

const int MAX_CLIENTS = 20;
const int EEPROM_SIZE = 4096;
ClientInfo clients[MAX_CLIENTS];
int clientCount = 0;

// Таймеры для автоматического сканирования
unsigned long lastAutoScan = 0;
const unsigned long AUTO_SCAN_INTERVAL = 10000; // 10 секунд

// Поиск клиента по MAC адресу
int findClientByMAC(const String& mac) {
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].mac == mac) {
      return i;
    }
  }
  return -1;
}

// Поиск клиента по IP адресу
int findClientByIP(IPAddress ip) {
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].ip == ip) {
      return i;
    }
  }
  return -1;
}

// Добавление нового клиента
bool addClient(IPAddress ip, const String& mac) {
  // Проверяем, не существует ли уже клиент с таким MAC
  int existingIndex = findClientByMAC(mac);
  if (existingIndex != -1) {
    // Обновляем существующего клиента
    clients[existingIndex].ip = ip;
    clients[existingIndex].lastSeen = millis();
    clients[existingIndex].connected = true;
    
    Serial.print("Обновлен клиент: ");
    Serial.print(ip.toString());
    Serial.print(" MAC: ");
    Serial.println(mac);
    return true;
  }
  
  // Проверяем, есть ли свободное место
  if (clientCount >= MAX_CLIENTS) {
    Serial.println("Достигнут лимит клиентов!");
    return false;
  }
  
  // Добавляем нового клиента
  clients[clientCount].ip = ip;
  clients[clientCount].mac = mac;
  clients[clientCount].hostname = "Device-" + String(clientCount + 1);
  clients[clientCount].comment = "";
  clients[clientCount].lastSeen = millis();
  clients[clientCount].connected = true;
  clients[clientCount].isWebServer = true; // Все устройства считаем веб-серверами по умолчанию
  clients[clientCount].isSensorDevice = false;
  
  Serial.print("Добавлен новый клиент: ");
  Serial.print(ip.toString());
  Serial.print(" MAC: ");
  Serial.println(mac);
  
  clientCount++;
  return true;
}

// Удаление клиента по индексу
void removeClient(int index) {
  if (index < 0 || index >= clientCount) return;
  
  Serial.print("Удален клиент: ");
  Serial.print(clients[index].ip.toString());
  Serial.print(" MAC: ");
  Serial.println(clients[index].mac);
  
  // Сдвигаем массив
  for (int i = index; i < clientCount - 1; i++) {
    clients[i] = clients[i + 1];
  }
  
  clientCount--;
}

// Пометить клиента как отключенного
void markClientDisconnected(int index) {
  if (index < 0 || index >= clientCount) return;
  
  clients[index].connected = false;
  
  Serial.print("Клиент отключен: ");
  Serial.print(clients[index].ip.toString());
  Serial.print(" MAC: ");
  Serial.println(clients[index].mac);
}

// Загрузка данных из EEPROM
void loadFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  
  int address = 0;
  int storedCount = 0;
  
  // Читаем количество сохраненных клиентов
  EEPROM.get(address, storedCount);
  address += sizeof(storedCount);
  
  Serial.print("Загружено клиентов из EEPROM: ");
  Serial.println(storedCount);
  
  // Создаем временный массив для загрузки данных из EEPROM
  StoredClient storedClients[MAX_CLIENTS];
  
  for (int i = 0; i < storedCount && i < MAX_CLIENTS; i++) {
    EEPROM.get(address, storedClients[i]);
    address += sizeof(StoredClient);
  }
  
  EEPROM.end();
  
  // Применяем загруженные данные к текущим клиентам по MAC-адресу
  for (int i = 0; i < storedCount; i++) {
    for (int j = 0; j < clientCount; j++) {
      if (clients[j].mac == String(storedClients[i].mac)) {
        clients[j].hostname = String(storedClients[i].hostname);
        clients[j].comment = String(storedClients[i].comment);
        clients[j].isSensorDevice = storedClients[i].isSensorDevice;
        break;
      }
    }
  }
}

// Сохранение данных в EEPROM
void saveToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  
  int address = 0;
  int countToSave = 0;
  
  // Считаем клиентов с непустыми hostname или comment
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].hostname != "Device-" + String(i + 1) || clients[i].comment != "" || clients[i].isSensorDevice) {
      countToSave++;
    }
  }
  
  // Сохраняем количество
  EEPROM.put(address, countToSave);
  address += sizeof(countToSave);
  
  // Сохраняем данные клиентов
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].hostname != "Device-" + String(i + 1) || clients[i].comment != "" || clients[i].isSensorDevice) {
      StoredClient stored;
      strncpy(stored.mac, clients[i].mac.c_str(), sizeof(stored.mac) - 1);
      strncpy(stored.hostname, clients[i].hostname.c_str(), sizeof(stored.hostname) - 1);
      strncpy(stored.comment, clients[i].comment.c_str(), sizeof(stored.comment) - 1);
      stored.isSensorDevice = clients[i].isSensorDevice;
      
      stored.mac[sizeof(stored.mac) - 1] = '\0';
      stored.hostname[sizeof(stored.hostname) - 1] = '\0';
      stored.comment[sizeof(stored.comment) - 1] = '\0';
      
      EEPROM.put(address, stored);
      address += sizeof(StoredClient);
    }
  }
  
  EEPROM.commit();
  EEPROM.end();
  
  Serial.print("Сохранено клиентов в EEPROM: ");
  Serial.println(countToSave);
}

// Функция для получения подключенных станций через softAP
void getConnectedStations() {
  struct station_info *station_list = wifi_softap_get_station_info();
  
  // Сначала помечаем всех клиентов как отключенных
  for (int i = 0; i < clientCount; i++) {
    clients[i].connected = false;
  }
  
  if (station_list != NULL) {
    struct station_info *station = station_list;
    int foundCount = 0;
    
    while (station != NULL && foundCount < MAX_CLIENTS) {
      IPAddress stationIP(station->ip.addr);
      
      // Преобразуем MAC адрес в строку
      char macStr[18];
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               station->bssid[0], station->bssid[1], station->bssid[2],
               station->bssid[3], station->bssid[4], station->bssid[5]);
      String mac = String(macStr);
      
      // Добавляем или обновляем клиента
      addClient(stationIP, mac);
      
      station = STAILQ_NEXT(station, next);
      foundCount++;
    }
    
    wifi_softap_free_station_info();
    Serial.print("Найдено подключенных станций: ");
    Serial.println(foundCount);
  } else {
    Serial.println("Нет подключенных станций");
  }
}

// Полное сканирование сети
void scanNetwork() {
  Serial.println("=== НАЧАЛО СКАНИРОВАНИЯ СЕТИ ===");
  
  // Получаем подключенные станции
  getConnectedStations();
  
  // Загружаем сохраненные данные из EEPROM (привязка по MAC)
  loadFromEEPROM();
  
  // Удаляем старые отключенные клиенты (которые не видели более 5 минут)
  unsigned long currentTime = millis();
  for (int i = clientCount - 1; i >= 0; i--) {
    if (!clients[i].connected && (currentTime - clients[i].lastSeen > 300000)) { // 5 минут
      removeClient(i);
    }
  }
  
  Serial.print("=== ВСЕГО КЛИЕНТОВ В БАЗЕ: ");
  Serial.println(clientCount);
  Serial.print("Подключено: ");
  int connectedCount = 0;
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].connected) connectedCount++;
  }
  Serial.println(connectedCount);
  Serial.println();
}

// REST API для получения списка клиентов
void handleApiClients() {
  Serial.println("GET /api/clients");
  
  // Устанавливаем CORS заголовки
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
  server.sendHeader("Connection", "close");
  
  DynamicJsonDocument doc(4096);
  JsonArray clientsArray = doc.to<JsonArray>();
  
  for (int i = 0; i < clientCount; i++) {
    JsonObject clientObj = clientsArray.createNestedObject();
    clientObj["id"] = i;
    clientObj["ip"] = clients[i].ip.toString();
    clientObj["mac"] = clients[i].mac;
    clientObj["hostname"] = clients[i].hostname;
    clientObj["comment"] = clients[i].comment;
    clientObj["isWebServer"] = clients[i].isWebServer;
    clientObj["isSensorDevice"] = clients[i].isSensorDevice;
    clientObj["connected"] = clients[i].connected;
    clientObj["lastSeen"] = clients[i].lastSeen;
  }
  
  String response;
  serializeJson(doc, response);
  
  server.send(200, "application/json", response);
}

// REST API для обновления информации о клиенте
void handleApiUpdateClient() {
  Serial.println("PUT /api/update");
  
  if (server.method() == HTTP_OPTIONS) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    server.send(200, "text/plain", "");
    return;
  }
  
  // Устанавливаем CORS заголовки для всех методов
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
  
  if (server.method() == HTTP_PUT || server.method() == HTTP_POST) {
    String mac = server.arg("mac");
    String hostname = server.arg("hostname");
    String comment = server.arg("comment");
    bool isSensorDevice = server.arg("isSensorDevice") == "true";
    
    Serial.print("Обновление клиента: MAC=");
    Serial.print(mac);
    Serial.print(", hostname=");
    Serial.print(hostname);
    Serial.print(", comment=");
    Serial.print(comment);
    Serial.print(", isSensorDevice=");
    Serial.println(isSensorDevice);
    
    bool clientFound = false;
    for (int i = 0; i < clientCount; i++) {
      if (clients[i].mac == mac) {
        clients[i].hostname = hostname;
        clients[i].comment = comment;
        clients[i].isSensorDevice = isSensorDevice;
        clientFound = true;
        
        // Сохраняем в EEPROM
        saveToEEPROM();
        
        server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Данные успешно обновлены\"}");
        return;
      }
    }
    
    if (!clientFound) {
      server.send(404, "application/json", "{\"status\":\"error\", \"message\":\"Клиент не найден\"}");
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\", \"message\":\"Метод не поддерживается\"}");
  }
}

// REST API для сканирования сети
void handleApiScan() {
  Serial.println("GET /api/scan");
  
  // Устанавливаем CORS заголовки
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
  server.sendHeader("Connection", "close");
  
  scanNetwork();
  server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Сканирование завершено\", \"count\":" + String(clientCount) + "}");
}

// REST API для получения статистики
void handleApiStats() {
  Serial.println("GET /api/stats");
  
  // Устанавливаем CORS заголовки
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
  server.sendHeader("Connection", "close");
  
  int connectedCount = 0;
  int webServerCount = 0;
  int sensorCount = 0;
  
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].connected) connectedCount++;
    if (clients[i].isWebServer) webServerCount++;
    if (clients[i].isSensorDevice) sensorCount++;
  }
  
  String response = "{";
  response += "\"totalClients\":" + String(clientCount) + ",";
  response += "\"connectedClients\":" + String(connectedCount) + ",";
  response += "\"webServers\":" + String(webServerCount) + ",";
  response += "\"sensorDevices\":" + String(sensorCount) + ",";
  response += "\"uptime\":" + String(millis());
  response += "}";
  
  server.send(200, "application/json", response);
}

// Главная страница
void handleRoot() {
  String html = R"=====(
<!DOCTYPE HTML>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>ESP8266 Шлюз</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
    .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; text-align: center; }
    .nav { text-align: center; margin: 20px 0; }
    .nav a { margin: 0 10px; padding: 10px 20px; background: #007cba; color: white; text-decoration: none; border-radius: 5px; }
    .nav a:hover { background: #005a87; }
    .info-card { background: #e8f4fd; padding: 15px; margin: 15px 0; border-radius: 5px; border-left: 4px solid #007cba; }
    .stats { display: flex; justify-content: space-around; margin: 20px 0; }
    .stat-card { background: #f8f9fa; padding: 15px; border-radius: 8px; text-align: center; flex: 1; margin: 0 10px; border: 1px solid #dee2e6; }
    .stat-number { font-size: 24px; font-weight: bold; color: #007cba; }
    .stat-label { font-size: 14px; color: #6c757d; }
  </style>
</head>
<body>
  <div class='container'>
    <h1>ESP8266 Точка Доступа</h1>
    
    <div class='nav'>
      <a href='/'>Главная</a>
      <a href='/clients'>Подключенные устройства</a>
      <a href='/scan'>Сканировать сеть</a>
    </div>
    
    <div class='info-card'>
      <h3>Информация о точке доступа:</h3>
      <p><strong>SSID:</strong> ESP8266_AP</p>
      <p><strong>IP адрес шлюза:</strong> 192.168.5.1</p>
      <p><strong>Маска подсети:</strong> 255.255.255.0</p>
      <p><strong>Количество подключенных клиентов:</strong> <span id='connectedCount'>0</span></p>
    </div>
    
    <div class='stats' id='statsContainer'>
      <div class='stat-card'><div class='stat-number' id='totalClients'>0</div><div class='stat-label'>Всего устройств</div></div>
      <div class='stat-card'><div class='stat-number' id='connectedClients'>0</div><div class='stat-label'>Подключено</div></div>
      <div class='stat-card'><div class='stat-number' id='webServers'>0</div><div class='stat-label'>Веб-серверы</div></div>
      <div class='stat-card'><div class='stat-number' id='sensorDevices'>0</div><div class='stat-label'>Сенсоры</div></div>
    </div>
    
    <div>
      <h3>REST API Endpoints:</h3>
      <ul>
        <li><strong>GET /api/clients</strong> - список клиентов</li>
        <li><strong>PUT /api/update</strong> - обновить клиента</li>
        <li><strong>GET /api/scan</strong> - сканировать сеть</li>
        <li><strong>GET /api/stats</strong> - статистика</li>
      </ul>
    </div>
  </div>
  
  <script>
    function loadStats() {
      fetch('/api/stats')
        .then(response => response.json())
        .then(data => {
          document.getElementById('totalClients').textContent = data.totalClients;
          document.getElementById('connectedClients').textContent = data.connectedClients;
          document.getElementById('webServers').textContent = data.webServers;
          document.getElementById('sensorDevices').textContent = data.sensorDevices;
          document.getElementById('connectedCount').textContent = data.connectedClients;
        })
        .catch(error => console.error('Error loading stats:', error));
    }
    document.addEventListener('DOMContentLoaded', function() {
      loadStats();
      setInterval(loadStats, 5000); // Обновление каждые 5 секунд
    });
  </script>
</body>
</html>
)=====";

  server.send(200, "text/html", html);
}

// Страница клиентов
void handleClients() {
  String html = R"=====(
<!DOCTYPE HTML>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>Подключенные устройства</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
    .container { max-width: 1300px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; text-align: center; }
    .nav { text-align: center; margin: 20px 0; }
    .nav a { margin: 0 10px; padding: 10px 20px; background: #007cba; color: white; text-decoration: none; border-radius: 5px; }
    .nav a:hover { background: #005a87; }
    table { width: 100%; border-collapse: collapse; margin: 20px 0; font-size: 14px; }
    th, td { padding: 10px; text-align: left; border-bottom: 1px solid #ddd; }
    th { background-color: #007cba; color: white; }
    tr:hover { background-color: #f5f5f5; }
    .refresh-btn { background: #28a745; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; margin: 10px 5px; }
    .refresh-btn:hover { background: #218838; }
    .scan-btn { background: #17a2b8; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; margin: 10px 5px; }
    .scan-btn:hover { background: #138496; }
    .web-server { background: #d4edda; }
    .sensor-device { background: #fff3cd; }
    .disconnected { background: #f8d7da; opacity: 0.7; }
    .web-server-link { color: #155724; font-weight: bold; }
    .edit-btn { background: #ffc107; color: black; padding: 5px 10px; border: none; border-radius: 3px; cursor: pointer; margin: 2px; }
    .edit-btn:hover { background: #e0a800; }
    .modal { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.5); }
    .modal-content { background-color: white; margin: 5% auto; padding: 20px; border-radius: 10px; width: 500px; max-width: 90%; }
    .form-group { margin: 15px 0; }
    .form-group label { display: block; margin-bottom: 5px; font-weight: bold; }
    .form-group input, .form-group textarea { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
    .checkbox-group { display: flex; align-items: center; margin: 15px 0; }
    .checkbox-group input { width: auto; margin-right: 10px; }
    .save-btn { background: #28a745; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; margin-right: 10px; }
    .cancel-btn { background: #6c757d; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; }
    .status-indicator { display: inline-block; width: 10px; height: 10px; border-radius: 50%; margin-right: 5px; }
    .status-web { background-color: #28a745; }
    .status-sensor { background-color: #ffc107; }
    .status-connected { background-color: #28a745; }
    .status-disconnected { background-color: #dc3545; }
    .loading { text-align: center; padding: 20px; color: #6c757d; }
    .error { color: #dc3545; background: #f8d7da; padding: 10px; border-radius: 5px; margin: 10px 0; }
  </style>
</head>
<body>
  <div class='container'>
    <h1>Подключенные устройства</h1>
    
    <div class='nav'>
      <a href='/'>Главная</a>
      <a href='/clients'>Подключенные устройства</a>
      <a href='/scan'>Сканировать сеть</a>
    </div>
    
    <div>
      <button class='refresh-btn' onclick='loadClients()'>Обновить список</button>
      <button class='scan-btn' onclick='scanNetwork()'>Сканировать сеть</button>
    </div>
    
    <div id='clientsTable'>
      <div class='loading'>Загрузка списка устройств...</div>
    </div>
    
    <!-- Модальное окно для редактирования -->
    <div id='editModal' class='modal'>
      <div class='modal-content'>
        <h2>Редактирование устройства</h2>
        <form id='editForm'>
          <input type='hidden' id='editMac'>
          <div class='form-group'>
            <label for='editHostname'>Имя хоста:</label>
            <input type='text' id='editHostname' maxlength='30'>
          </div>
          <div class='form-group'>
            <label for='editComment'>Комментарий:</label>
            <textarea id='editComment' rows='3' maxlength='60'></textarea>
          </div>
          <div class='checkbox-group'>
            <input type='checkbox' id='editIsSensorDevice'>
            <label for='editIsSensorDevice'>Сенсорное устройство</label>
          </div>
          <div>
            <button type='button' class='save-btn' onclick='saveClient()'>Сохранить</button>
            <button type='button' class='cancel-btn' onclick='closeModal()'>Отмена</button>
          </div>
        </form>
      </div>
    </div>
  </div>
  
  <script>
    let clientsData = [];
    
    function loadClients() {
      const table = document.getElementById('clientsTable');
      table.innerHTML = '<div class="loading">Загрузка списка устройств...</div>';
      
      fetch('/api/clients')
        .then(response => {
          if (!response.ok) {
            throw new Error('Ошибка сети: ' + response.status);
          }
          return response.json();
        })
        .then(data => {
          clientsData = data;
          renderTable();
        })
        .catch(error => {
          console.error('Error loading clients:', error);
          table.innerHTML = '<div class="error">Ошибка загрузки данных: ' + error.message + '</div>';
        });
    }
    
    function scanNetwork() {
      const table = document.getElementById('clientsTable');
      table.innerHTML = '<div class="loading">Сканирование сети...</div>';
      
      fetch('/api/scan')
        .then(response => {
          if (!response.ok) {
            throw new Error('Ошибка сети: ' + response.status);
          }
          return response.json();
        })
        .then(data => {
          console.log('Scan completed:', data);
          loadClients();
        })
        .catch(error => {
          console.error('Error scanning network:', error);
          table.innerHTML = '<div class="error">Ошибка сканирования: ' + error.message + '</div>';
        });
    }
    
    function renderTable() {
      const table = document.getElementById('clientsTable');
      if (clientsData.length === 0) {
        table.innerHTML = '<p>Нет подключенных устройств</p>';
        return;
      }
      
      let html = '<table><thead><tr><th>#</th><th>IP Адрес</th><th>MAC Адрес</th><th>Имя хоста</th><th>Комментарий</th><th>Статус</th><th>Действия</th></tr></thead><tbody>';
      
      clientsData.forEach((client, index) => {
        let rowClass = '';
        if (!client.connected) rowClass = 'disconnected';
        else if (client.isWebServer && client.isSensorDevice) rowClass = 'web-server sensor-device';
        else if (client.isWebServer) rowClass = 'web-server';
        else if (client.isSensorDevice) rowClass = 'sensor-device';
        
        html += '<tr class="' + rowClass + '">';
        html += '<td>' + (index + 1) + '</td>';
        html += '<td>' + client.ip + '</td>';
        html += '<td>' + client.mac + '</td>';
        html += '<td>' + client.hostname + '</td>';
        html += '<td>' + client.comment + '</td>';
        html += '<td>';
        html += '<span class="status-indicator ' + (client.connected ? 'status-connected' : 'status-disconnected') + '" title="' + (client.connected ? 'Подключен' : 'Отключен') + '"></span>';
        if (client.isWebServer) html += '<span class="status-indicator status-web" title="Веб-сервер"></span>';
        if (client.isSensorDevice) html += '<span class="status-indicator status-sensor" title="Сенсорное устройство"></span>';
        html += (client.connected ? 'Подключен ' : 'Отключен ') + (client.isWebServer ? 'Веб-сервер ' : '') + (client.isSensorDevice ? 'Сенсор' : '');
        html += '</td>';
        html += '<td>';
        if (client.isWebServer && client.connected) {
          html += '<a class="web-server-link" href="http://' + client.ip + '" target="_blank">Открыть</a> | ';
        }
        html += '<button class="edit-btn" onclick="editClient(\'' + client.mac + '\')">Редактировать</button>';
        html += '</td>';
        html += '</tr>';
      });
      
      html += '</tbody></table>';
      html += '<p><strong>Всего устройств:</strong> ' + clientsData.length + '</p>';
      table.innerHTML = html;
    }
    
    function editClient(mac) {
      const client = clientsData.find(c => c.mac === mac);
      if (client) {
        document.getElementById('editMac').value = client.mac;
        document.getElementById('editHostname').value = client.hostname;
        document.getElementById('editComment').value = client.comment;
        document.getElementById('editIsSensorDevice').checked = client.isSensorDevice;
        document.getElementById('editModal').style.display = 'block';
      }
    }
    
    function closeModal() {
      document.getElementById('editModal').style.display = 'none';
    }
    
    function saveClient() {
      const mac = document.getElementById('editMac').value;
      const hostname = document.getElementById('editHostname').value;
      const comment = document.getElementById('editComment').value;
      const isSensorDevice = document.getElementById('editIsSensorDevice').checked;
      
      const formData = new URLSearchParams();
      formData.append('mac', mac);
      formData.append('hostname', hostname);
      formData.append('comment', comment);
      formData.append('isSensorDevice', isSensorDevice);
      
      fetch('/api/update', {
        method: 'PUT',
        body: formData
      })
      .then(response => {
        if (!response.ok) {
          throw new Error('Ошибка сети: ' + response.status);
        }
        return response.json();
      })
      .then(data => {
        console.log('Update response:', data);
        closeModal();
        loadClients();
        if (data.status === 'success') {
          alert('Данные успешно сохранены!');
        } else {
          alert('Ошибка: ' + data.message);
        }
      })
      .catch(error => {
        console.error('Error updating client:', error);
        alert('Ошибка сохранения: ' + error.message);
      });
    }
    
    // Закрытие модального окна при клике вне его
    window.onclick = function(event) {
      const modal = document.getElementById('editModal');
      if (event.target === modal) {
        closeModal();
      }
    }
    
    // Автоматическая загрузка при открытии страницы
    document.addEventListener('DOMContentLoaded', function() {
      loadClients();
      setInterval(loadClients, 10000); // Автообновление каждые 10 секунд
    });
  </script>
</body>
</html>
)=====";

  server.send(200, "text/html", html);
}

// Обработчик сканирования
void handleScan() {
  scanNetwork();
  server.sendHeader("Location", "/clients");
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Запуск точки доступа...");
  
  // Настройка WiFi в режиме точки доступа
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ssid, password);
  
  Serial.println("Точка доступа запущена");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("IP адрес: ");
  Serial.println(WiFi.softAPIP());
  
  // Настройка маршрутов веб-сервера
  server.on("/", handleRoot);
  server.on("/clients", handleClients);
  server.on("/scan", handleScan);
  server.on("/api/clients", handleApiClients);
  server.on("/api/update", handleApiUpdateClient);
  server.on("/api/scan", handleApiScan);
  server.on("/api/stats", handleApiStats);
  
  // Обработчик для favicon
  server.on("/favicon.ico", []() {
    server.send(404, "text/plain", "Not Found");
  });
  
  // Обработчик для OPTIONS запросов (CORS)
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
      server.send(200, "text/plain", "");
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });
  
  server.begin();
  Serial.println("HTTP сервер запущен на 192.168.5.1");
  
  // Первоначальное сканирование сети
  scanNetwork();
}

void loop() {
  server.handleClient();
  
  // Автоматическое сканирование сети каждые 10 секунд
  unsigned long currentTime = millis();
  if (currentTime - lastAutoScan > AUTO_SCAN_INTERVAL) {
    scanNetwork();
    lastAutoScan = currentTime;
  }
  
  delay(100);
}
