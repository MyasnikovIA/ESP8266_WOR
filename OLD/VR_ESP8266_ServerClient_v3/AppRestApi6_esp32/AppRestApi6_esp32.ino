#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

// Настройки по умолчанию
const char* default_ssid = "MyApp";
const char* default_password = "12345678";
IPAddress default_ip(192, 168, 1, 1);
IPAddress default_subnet(255, 255, 255, 0);

// Структура для хранения настроек
struct Settings {
  char ssid[32];
  char password[64];
  uint8_t subnet_part;
  bool initialized;
};

// Структура для информации об устройствах
struct DeviceInfo {
  char mac[18];
  char deviceName[32];
  char deviceComment[256];
  bool isSensorDevice;
  uint8_t ip_last_octet;
};

WebServer server(80);
Settings settings;
std::vector<DeviceInfo> devices;

// EEPROM адреса
#define EEPROM_SIZE 4096
#define SETTINGS_ADDR 0
#define DEVICES_ADDR 512

// Прототипы функций
String generateIPAddress(const char* mac, uint8_t custom_last_octet);

void setup() {
  Serial.begin(115200);
  Serial.println("Запуск ESP32...");
  
  // Инициализация EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Загрузка настроек
  loadSettings();
  
  // Загрузка информации об устройствах
  loadDevices();
  
  // Настройка WiFi в режиме точки доступа
  setupAP();
  
  // Настройка веб-сервера
  setupWebServer();
  
  Serial.println("====================================");
  Serial.println("Точка доступа запущена успешно!");
  Serial.print("SSID: ");
  Serial.println(settings.ssid);
  Serial.print("Пароль: ");
  Serial.println(settings.password);
  Serial.print("IP адрес: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("Маска подсети: 255.255.255.0");
  Serial.println("====================================");
}

void loop() {
  server.handleClient();
  delay(2);
}

void setupAP() {
  Serial.println("Настройка точки доступа...");
  
  // Останавливаем предыдущую точку доступа если была
  WiFi.softAPdisconnect(true);
  delay(1000);
  
  // Создание подсети на основе настроек
  IPAddress local_ip(192, 168, settings.subnet_part, 1);
  IPAddress gateway(192, 168, settings.subnet_part, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  Serial.print("Попытка запуска точки доступа: ");
  Serial.println(settings.ssid);
  
  // Запускаем точку доступа
  bool apStarted = WiFi.softAP(settings.ssid, settings.password);
  
  if (!apStarted) {
    Serial.println("ОШИБКА: Не удалось запустить точку доступа!");
    // Пытаемся перезапустить с настройками по умолчанию
    Serial.println("Попытка перезапуска с настройками по умолчанию...");
    strcpy(settings.ssid, default_ssid);
    strcpy(settings.password, default_password);
    settings.subnet_part = 1;
    apStarted = WiFi.softAP(settings.ssid, settings.password);
    
    if (!apStarted) {
      Serial.println("КРИТИЧЕСКАЯ ОШИБКА: Не удалось запустить точку доступа даже с настройками по умолчанию!");
      return;
    }
  }
  
  delay(1000);
  
  // Настраиваем сетевые параметры
  bool configSuccess = WiFi.softAPConfig(local_ip, gateway, subnet);
  
  if (!configSuccess) {
    Serial.println("Предупреждение: Не удалось настроить сетевые параметры");
  }
  
  delay(1000);
  
  Serial.println("Точка доступа успешно запущена!");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("Количество подключенных устройств: ");
  Serial.println(WiFi.softAPgetStationNum());
}

void setupWebServer() {
  // Статические файлы (HTML, CSS, JS)
  server.on("/", HTTP_GET, []() {
    String html = getHTML();
    server.send(200, "text/html", html);
  });
  
  // REST API - получение настроек
  server.on("/api/settings", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = getSettingsJSON();
    server.send(200, "application/json", json);
  });
  
  // REST API - сохранение настроек
  server.on("/api/settings", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    saveSettings();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });
  
  // REST API - получение списка подключенных устройств
  server.on("/api/devices", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = getDevicesJSON();
    server.send(200, "application/json", json);
  });
  
  // REST API - получение списка сенсорных устройств
  server.on("/api/sensors", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = getSensorsJSON();
    server.send(200, "application/json", json);
  });
  
  // REST API - сохранение информации об устройстве
  server.on("/api/device", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    saveDeviceInfo();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // REST API для приема данных от сенсоров
  server.on("/api/sensor-data", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");
    StaticJsonDocument<512> doc;
    deserializeJson(doc, body);
    
    String mac = doc["mac"];
    float temperature = doc["temperature"];
    float humidity = doc["humidity"];
    
    Serial.print("Данные от сенсора ");
    Serial.print(mac);
    Serial.print(": Температура=");
    Serial.print(temperature);
    Serial.print(", Влажность=");
    Serial.println(humidity);
    
    server.send(200, "application/json", "{\"status\":\"received\"}");
  });
  
  // Запуск сервера
  server.begin();
  Serial.println("HTTP сервер запущен на порту 80");
  
  // Включение CORS для OPTIONS запросов
  server.onNotFound([]() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    if (server.method() == HTTP_OPTIONS) {
      server.send(200);
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });
}

void loadSettings() {
  Serial.println("Загрузка настроек из EEPROM...");
  
  // Читаем настройки из EEPROM
  EEPROM.get(SETTINGS_ADDR, settings);
  
  // Проверяем, инициализированы ли настройки
  if (!settings.initialized) {
    Serial.println("Настройки не найдены в EEPROM. Использую настройки по умолчанию.");
    
    // Использование настроек по умолчанию
    strcpy(settings.ssid, default_ssid);
    strcpy(settings.password, default_password);
    settings.subnet_part = 1;
    settings.initialized = true;
    
    // Сохраняем настройки по умолчанию
    saveSettingsToEEPROM();
    
    Serial.print("Создана новая сеть: ");
    Serial.println(settings.ssid);
  } else {
    Serial.println("Настройки загружены из EEPROM:");
    Serial.print("SSID: ");
    Serial.println(settings.ssid);
    Serial.print("Подсеть: 192.168.");
    Serial.print(settings.subnet_part);
    Serial.println(".1");
  }
}

void saveSettings() {
  String body = server.arg("plain");
  StaticJsonDocument<512> doc;
  deserializeJson(doc, body);
  
  Serial.println("Сохранение новых настроек...");
  
  if (doc.containsKey("ssid")) {
    String new_ssid = doc["ssid"];
    if (new_ssid.length() > 0) {
      strcpy(settings.ssid, new_ssid.c_str());
      Serial.print("Новый SSID: ");
      Serial.println(settings.ssid);
    }
  }
  if (doc.containsKey("password")) {
    String new_password = doc["password"];
    if (new_password.length() >= 8) {
      strcpy(settings.password, new_password.c_str());
      Serial.println("Пароль обновлен");
    }
  }
  if (doc.containsKey("subnet_part")) {
    settings.subnet_part = doc["subnet_part"];
    Serial.print("Новая подсеть: 192.168.");
    Serial.print(settings.subnet_part);
    Serial.println(".1");
  }
  
  // Сохраняем в EEPROM
  saveSettingsToEEPROM();
  
  // Перезапуск точки доступа с новыми настройками
  Serial.println("Перезапуск точки доступа с новыми настройками...");
  setupAP();
}

void saveSettingsToEEPROM() {
  EEPROM.put(SETTINGS_ADDR, settings);
  bool success = EEPROM.commit();
  
  if (success) {
    Serial.println("Настройки успешно сохранены в EEPROM");
  } else {
    Serial.println("ОШИБКА: Не удалось сохранить настройки в EEPROM");
  }
}

void loadDevices() {
  devices.clear();
  int addr = DEVICES_ADDR;
  int count = EEPROM.read(addr++);
  
  // Проверяем валидность количества устройств
  if (count > 100) count = 0; // Защита от corrupted data
  
  for (int i = 0; i < count; i++) {
    DeviceInfo device;
    EEPROM.get(addr, device);
    addr += sizeof(DeviceInfo);
    devices.push_back(device);
  }
  
  Serial.print("Загружено устройств из EEPROM: ");
  Serial.println(devices.size());
}

void saveDeviceInfo() {
  String body = server.arg("plain");
  StaticJsonDocument<512> doc;
  deserializeJson(doc, body);
  
  String mac = doc["mac"];
  String deviceName = doc["deviceName"];
  String deviceComment = doc["deviceComment"];
  bool isSensorDevice = doc["isSensorDevice"];
  
  // Поиск устройства по MAC
  bool found = false;
  for (auto& device : devices) {
    if (String(device.mac) == mac) {
      strcpy(device.deviceName, deviceName.c_str());
      strcpy(device.deviceComment, deviceComment.c_str());
      device.isSensorDevice = isSensorDevice;
      found = true;
      Serial.print("Обновлена информация для устройства: ");
      Serial.println(mac);
      break;
    }
  }
  
  // Если устройство не найдено, добавляем новое
  if (!found) {
    DeviceInfo newDevice;
    strcpy(newDevice.mac, mac.c_str());
    strcpy(newDevice.deviceName, deviceName.c_str());
    strcpy(newDevice.deviceComment, deviceComment.c_str());
    newDevice.isSensorDevice = isSensorDevice;
    
    // Генерируем стабильный IP на основе MAC
    newDevice.ip_last_octet = generateIPLastOctet(mac.c_str());
    
    devices.push_back(newDevice);
    Serial.print("Добавлено новое устройство: ");
    Serial.println(mac);
  }
  
  // Сохранение в EEPROM
  saveDevicesToEEPROM();
}

void saveDevicesToEEPROM() {
  int addr = DEVICES_ADDR;
  EEPROM.write(addr++, devices.size());
  
  for (const auto& device : devices) {
    EEPROM.put(addr, device);
    addr += sizeof(DeviceInfo);
  }
  bool success = EEPROM.commit();
  
  if (success) {
    Serial.print("Устройства сохранены в EEPROM: ");
    Serial.println(devices.size());
  } else {
    Serial.println("ОШИБКА сохранения устройств в EEPROM");
  }
}

String getSettingsJSON() {
  StaticJsonDocument<512> doc;
  doc["ssid"] = settings.ssid;
  doc["password"] = settings.password;
  doc["subnet_part"] = settings.subnet_part;
  
  String json;
  serializeJson(doc, json);
  return json;
}

String getDevicesJSON() {
  // Обновление списка подключенных устройств
  updateConnectedDevices();
  
  StaticJsonDocument<4096> doc;
  JsonArray devicesArray = doc.to<JsonArray>();
  
  for (const auto& device : devices) {
    JsonObject deviceObj = devicesArray.createNestedObject();
    deviceObj["mac"] = device.mac;
    deviceObj["deviceName"] = device.deviceName;
    deviceObj["deviceComment"] = device.deviceComment;
    deviceObj["isSensorDevice"] = device.isSensorDevice;
    
    // Генерация IP адреса
    String ip = generateIPAddress(device.mac, device.ip_last_octet);
    deviceObj["ip"] = ip;
  }
  
  String json;
  serializeJson(doc, json);
  return json;
}

String getSensorsJSON() {
  StaticJsonDocument<4096> doc;
  JsonArray sensorsArray = doc.to<JsonArray>();
  
  for (const auto& device : devices) {
    if (device.isSensorDevice) {
      JsonObject sensorObj = sensorsArray.createNestedObject();
      sensorObj["mac"] = device.mac;
      sensorObj["deviceName"] = device.deviceName;
      sensorObj["deviceComment"] = device.deviceComment;
      
      String ip = generateIPAddress(device.mac, device.ip_last_octet);
      sensorObj["ip"] = ip;
    }
  }
  
  String json;
  serializeJson(doc, json);
  return json;
}

void updateConnectedDevices() {
  // Получение списка подключенных станций
  wifi_sta_list_t station_list;
  if (esp_wifi_ap_get_sta_list(&station_list) == ESP_OK) {
    for (int i = 0; i < station_list.num; i++) {
      wifi_sta_info_t station = station_list.sta[i];
      
      // Форматирование MAC адреса
      char mac[18];
      sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X",
              station.mac[0], station.mac[1], station.mac[2],
              station.mac[3], station.mac[4], station.mac[5]);
      
      // Проверка, есть ли уже такое устройство
      bool found = false;
      for (auto& device : devices) {
        if (String(device.mac) == String(mac)) {
          found = true;
          break;
        }
      }
      
      // Добавление нового устройства
      if (!found) {
        DeviceInfo newDevice;
        strcpy(newDevice.mac, mac);
        strcpy(newDevice.deviceName, "Новое устройство");
        strcpy(newDevice.deviceComment, "");
        newDevice.isSensorDevice = false;
        newDevice.ip_last_octet = generateIPLastOctet(mac);
        
        devices.push_back(newDevice);
        Serial.print("Обнаружено новое подключенное устройство: ");
        Serial.println(mac);
      }
    }
    
    // Сохранение обновленного списка
    if (station_list.num > 0) {
      saveDevicesToEEPROM();
    }
  }
}

uint8_t generateIPLastOctet(const char* mac) {
  // Генерация стабильного последнего октета IP на основе MAC
  uint8_t mac_bytes[6];
  sscanf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", 
         &mac_bytes[0], &mac_bytes[1], &mac_bytes[2],
         &mac_bytes[3], &mac_bytes[4], &mac_bytes[5]);
  
  // Используем XOR для создания псевдослучайного но стабильного значения
  uint8_t last_octet = (mac_bytes[3] ^ mac_bytes[4] ^ mac_bytes[5]) % 253 + 2;
  return last_octet;
}

String generateIPAddress(const char* mac, uint8_t custom_last_octet) {
  uint8_t last_octet;
  
  if (custom_last_octet != 0) {
    // Используем сохраненное значение
    last_octet = custom_last_octet;
  } else {
    // Генерируем новое значение
    last_octet = generateIPLastOctet(mac);
  }
  
  char ip[16];
  sprintf(ip, "192.168.%d.%d", settings.subnet_part, last_octet);
  return String(ip);
}

String getIPByMAC(const char* mac) {
  // Поиск устройства в нашем списке
  for (const auto& device : devices) {
    if (String(device.mac) == String(mac)) {
      return generateIPAddress(mac, device.ip_last_octet);
    }
  }
  
  // Если устройство не найдено, генерируем новый IP
  return generateIPAddress(mac, 0);
}

String getHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>WiFi Configuration</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }
        .container { max-width: 1200px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .section { margin-bottom: 30px; padding: 20px; border: 1px solid #e0e0e0; border-radius: 5px; }
        .device-table { width: 100%; border-collapse: collapse; margin-top: 10px; }
        .device-table th, .device-table td { border: 1px solid #ddd; padding: 12px; text-align: left; }
        .device-table th { background-color: #4CAF50; color: white; }
        .device-table tr:nth-child(even) { background-color: #f2f2f2; }
        .device-table tr:hover { background-color: #e9f7e9; }
        .modal { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.5); }
        .modal-content { background-color: #fefefe; margin: 10% auto; padding: 20px; border-radius: 10px; width: 50%; max-width: 600px; box-shadow: 0 4px 20px rgba(0,0,0,0.2); }
        .close { color: #aaa; float: right; font-size: 28px; font-weight: bold; cursor: pointer; }
        .close:hover { color: black; }
        button { padding: 10px 20px; margin: 5px; cursor: pointer; background-color: #4CAF50; color: white; border: none; border-radius: 5px; font-size: 14px; }
        button:hover { background-color: #45a049; }
        button:disabled { background-color: #cccccc; cursor: not-allowed; }
        input, textarea, select { width: 100%; padding: 10px; margin: 8px 0; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        label { font-weight: bold; margin-top: 10px; display: block; }
        .form-group { margin-bottom: 15px; }
        .ip-link { color: #2196F3; text-decoration: none; cursor: pointer; }
        .ip-link:hover { text-decoration: underline; }
        .status { padding: 10px; margin: 10px 0; border-radius: 5px; }
        .status.success { background-color: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .status.error { background-color: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
        .status.info { background-color: #d1ecf1; color: #0c5460; border: 1px solid #bee5eb; }
        .checkbox-group { display: flex; align-items: center; margin: 10px 0; }
        .checkbox-group input { width: auto; margin-right: 10px; }
        .network-info { background-color: #e7f3ff; padding: 15px; border-radius: 5px; margin: 10px 0; }
        .system-info { background-color: #fff3cd; padding: 15px; border-radius: 5px; margin: 10px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🛰️ Настройка точки доступа</h1>
        
        <div class="system-info">
            <strong>Системная информация:</strong><br>
            • При первом запуске создается сеть "MyApp" с паролем "12345678"<br>
            • Все настройки сохраняются в энергонезависимую память<br>
            • После изменения настроек точка доступа перезапускается автоматически
        </div>
        
        <div class="network-info">
            <strong>Информация о сети:</strong><br>
            • Маска подсети: 255.255.255.0<br>
            • Диапазон IP: 192.168.XXX.2 - 192.168.XXX.254<br>
            • Шлюз: 192.168.XXX.1
        </div>
        
        <div class="section">
            <h2>📡 Настройки WiFi</h2>
            <form id="settingsForm">
                <div class="form-group">
                    <label for="ssid">SSID:</label>
                    <input type="text" id="ssid" name="ssid" required>
                </div>
                
                <div class="form-group">
                    <label for="password">Пароль (минимум 8 символов):</label>
                    <input type="password" id="password" name="password" required minlength="8">
                </div>
                
                <div class="form-group">
                    <label for="subnet_part">Подсеть (192.168.XXX.1):</label>
                    <select id="subnet_part" name="subnet_part">
                        <!-- Опции будут заполнены JavaScript -->
                    </select>
                </div>
                
                <button type="submit" id="saveSettingsBtn">💾 Сохранить настройки</button>
            </form>
            <div id="settingsStatus"></div>
        </div>

        <div class="section">
            <h2>📱 Подключенные устройства</h2>
            <button onclick="loadDevices()" id="refreshBtn">🔄 Обновить список</button>
            <div id="devicesCount"></div>
            <table class="device-table" id="devicesTable">
                <thead>
                    <tr>
                        <th>IP адрес</th>
                        <th>MAC адрес</th>
                        <th>Имя устройства</th>
                        <th>Комментарий</th>
                        <th>Тип</th>
                        <th>Действия</th>
                    </tr>
                </thead>
                <tbody id="devicesTableBody">
                    <tr>
                        <td colspan="6" style="text-align: center;">Загрузка...</td>
                    </tr>
                </tbody>
            </table>
        </div>
    </div>

    <!-- Модальное окно для редактирования устройства -->
    <div id="deviceModal" class="modal">
        <div class="modal-content">
            <span class="close">&times;</span>
            <h2>📝 Информация об устройстве</h2>
            <form id="deviceForm">
                <input type="hidden" id="editMac" name="mac">
                
                <div class="form-group">
                    <label for="editDeviceName">Имя устройства:</label>
                    <input type="text" id="editDeviceName" name="deviceName" placeholder="Введите имя устройства">
                </div>
                
                <div class="form-group">
                    <label for="editDeviceComment">Комментарий:</label>
                    <textarea id="editDeviceComment" name="deviceComment" rows="4" placeholder="Добавьте комментарий..."></textarea>
                </div>
                
                <div class="checkbox-group">
                    <input type="checkbox" id="editIsSensorDevice" name="isSensorDevice">
                    <label for="editIsSensorDevice">Сенсорное устройство</label>
                </div>
                
                <button type="submit" id="saveDeviceBtn">💾 Сохранить</button>
            </form>
            <div id="deviceStatus"></div>
        </div>
    </div>

    <script>
        let currentEditMac = '';
        
        // Загрузка настроек при старте
        document.addEventListener('DOMContentLoaded', function() {
            loadSettings();
            loadDevices();
            setupModal();
            setInterval(loadDevices, 10000); // Автообновление каждые 10 секунд
        });

        function setupModal() {
            const modal = document.getElementById('deviceModal');
            const closeBtn = document.querySelector('.close');
            
            closeBtn.onclick = function() {
                modal.style.display = 'none';
            }
            
            window.onclick = function(event) {
                if (event.target == modal) {
                    modal.style.display = 'none';
                }
            }
        }

        async function loadSettings() {
            try {
                showStatus('settingsStatus', 'Загрузка настроек...', 'info');
                const response = await fetch('/api/settings');
                if (!response.ok) throw new Error('Network error');
                
                const settings = await response.json();
                
                document.getElementById('ssid').value = settings.ssid;
                document.getElementById('password').value = settings.password;
                
                // Заполнение комбобокса подсети
                const subnetSelect = document.getElementById('subnet_part');
                subnetSelect.innerHTML = '';
                for (let i = 1; i <= 255; i++) {
                    const option = document.createElement('option');
                    option.value = i;
                    option.textContent = i;
                    if (i == settings.subnet_part) {
                        option.selected = true;
                    }
                    subnetSelect.appendChild(option);
                }
                
                showStatus('settingsStatus', 'Настройки загружены', 'success');
            } catch (error) {
                console.error('Ошибка загрузки настроек:', error);
                showStatus('settingsStatus', 'Ошибка загрузки настроек: ' + error.message, 'error');
            }
        }

        async function loadDevices() {
            try {
                const refreshBtn = document.getElementById('refreshBtn');
                refreshBtn.disabled = true;
                refreshBtn.textContent = '⏳ Загрузка...';
                
                const response = await fetch('/api/devices');
                if (!response.ok) throw new Error('Network error');
                
                const devices = await response.json();
                
                const tbody = document.getElementById('devicesTableBody');
                tbody.innerHTML = '';
                
                if (devices.length === 0) {
                    tbody.innerHTML = '<tr><td colspan="6" style="text-align: center;">Нет подключенных устройств</td></tr>';
                } else {
                    devices.forEach(device => {
                        const row = tbody.insertRow();
                        
                        // IP адрес с кликабельной ссылкой
                        const ipCell = row.insertCell();
                        if (device.ip && device.ip !== 'Неизвестно') {
                            const ipLink = document.createElement('a');
                            ipLink.className = 'ip-link';
                            ipLink.href = `http://${device.ip}`;
                            ipLink.target = '_blank';
                            ipLink.textContent = device.ip;
                            ipLink.title = 'Открыть в новой вкладке';
                            ipCell.appendChild(ipLink);
                        } else {
                            ipCell.textContent = device.ip;
                        }
                        
                        row.insertCell().textContent = device.mac;
                        row.insertCell().textContent = device.deviceName;
                        row.insertCell().textContent = device.deviceComment;
                        
                        const typeCell = row.insertCell();
                        typeCell.textContent = device.isSensorDevice ? '🎯 Сенсор' : '📱 Устройство';
                        
                        const actionsCell = row.insertCell();
                        const infoButton = document.createElement('button');
                        infoButton.textContent = '📝 Информация';
                        infoButton.onclick = () => editDevice(device);
                        actionsCell.appendChild(infoButton);
                    });
                }
                
                // Обновление счетчика устройств
                document.getElementById('devicesCount').innerHTML = 
                    `<div class="status success">Найдено устройств: ${devices.length}</div>`;
                
            } catch (error) {
                console.error('Ошибка загрузки устройств:', error);
                document.getElementById('devicesTableBody').innerHTML = 
                    '<tr><td colspan="6" style="text-align: center; color: red;">Ошибка загрузки: ' + error.message + '</td></tr>';
            } finally {
                const refreshBtn = document.getElementById('refreshBtn');
                refreshBtn.disabled = false;
                refreshBtn.textContent = '🔄 Обновить список';
            }
        }

        function editDevice(device) {
            document.getElementById('editMac').value = device.mac;
            document.getElementById('editDeviceName').value = device.deviceName;
            document.getElementById('editDeviceComment').value = device.deviceComment;
            document.getElementById('editIsSensorDevice').checked = device.isSensorDevice;
            
            document.getElementById('deviceModal').style.display = 'block';
            document.getElementById('deviceStatus').innerHTML = '';
        }

        function showStatus(elementId, message, type) {
            const element = document.getElementById(elementId);
            element.innerHTML = `<div class="status ${type}">${message}</div>`;
            setTimeout(() => {
                element.innerHTML = '';
            }, 5000);
        }

        // Обработчики форм
        document.getElementById('settingsForm').onsubmit = async function(e) {
            e.preventDefault();
            
            const saveBtn = document.getElementById('saveSettingsBtn');
            saveBtn.disabled = true;
            saveBtn.textContent = '⏳ Сохранение...';
            
            const formData = {
                ssid: document.getElementById('ssid').value,
                password: document.getElementById('password').value,
                subnet_part: parseInt(document.getElementById('subnet_part').value)
            };
            
            try {
                const response = await fetch('/api/settings', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify(formData)
                });
                
                if (!response.ok) throw new Error('Network error');
                
                showStatus('settingsStatus', 'Настройки успешно сохранены! Точка доступа перезапускается...', 'success');
            } catch (error) {
                console.error('Ошибка сохранения настроек:', error);
                showStatus('settingsStatus', 'Ошибка сохранения настроек: ' + error.message, 'error');
            } finally {
                saveBtn.disabled = false;
                saveBtn.textContent = '💾 Сохранить настройки';
            }
        };

        document.getElementById('deviceForm').onsubmit = async function(e) {
            e.preventDefault();
            
            const saveBtn = document.getElementById('saveDeviceBtn');
            saveBtn.disabled = true;
            saveBtn.textContent = '⏳ Сохранение...';
            
            const formData = {
                mac: document.getElementById('editMac').value,
                deviceName: document.getElementById('editDeviceName').value,
                deviceComment: document.getElementById('editDeviceComment').value,
                isSensorDevice: document.getElementById('editIsSensorDevice').checked
            };
            
            try {
                const response = await fetch('/api/device', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify(formData)
                });
                
                if (!response.ok) throw new Error('Network error');
                
                showStatus('deviceStatus', 'Информация об устройстве сохранена!', 'success');
                setTimeout(() => {
                    document.getElementById('deviceModal').style.display = 'none';
                    loadDevices(); // Обновление списка
                }, 1000);
                
            } catch (error) {
                console.error('Ошибка сохранения устройства:', error);
                showStatus('deviceStatus', 'Ошибка сохранения: ' + error.message, 'error');
            } finally {
                saveBtn.disabled = false;
                saveBtn.textContent = '💾 Сохранить';
            }
        };
    </script>
</body>
</html>
)rawliteral";
}
