#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
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
};

// Структура для информации о подключенных устройствах
struct ConnectedDevice {
  String ip;
  String mac;
  String device_name;
  String device_comment;
};

Settings settings;
ESP8266WebServer server(80);

// Для хранения информации о подключенных устройствах
ConnectedDevice connectedDevices[10];
int connectedDevicesCount = 0;

// Адреса в EEPROM для хранения настроек
const int EEPROM_SIZE = 512;
const int SETTINGS_ADDR = 0;

void setup() {
  Serial.begin(115200);
  
  // Инициализация EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Загрузка настроек из EEPROM
  loadSettings();
  
  // Запуск WiFi в зависимости от настроек
  setupWiFi();
  
  // Настройка веб-сервера
  setupWebServer();
  
  Serial.println("Device started");
}

void loop() {
  server.handleClient();
  
  // Обновление списка подключенных устройств каждые 5 секунд
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 5000) {
    updateConnectedDevices();
    lastUpdate = millis();
  }
}

void setupWiFi() {
  if (settings.ap_mode_enabled) {
    String ap_ssid = String(settings.ap_ssid);
    String ap_ip = "192.168." + String(settings.subnet) + ".1";
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(
      IPAddress(192, 168, settings.subnet, 1),
      IPAddress(192, 168, settings.subnet, 1),
      IPAddress(255, 255, 255, 0)
    );
    WiFi.softAP(ap_ssid.c_str(), settings.ap_password);
    
    Serial.println("AP Mode Started");
    Serial.println("SSID: " + ap_ssid);
    Serial.println("IP: " + ap_ip);
  } else {
    WiFi.mode(WIFI_STA);
  }
}

void setupWebServer() {
  // Основные маршруты
  server.on("/", handleRoot);
  server.on("/settings", handleSettings);
  server.on("/wifi-scan", handleWifiScan);
  server.on("/device-config", handleDeviceConfig);
  server.on("/device-control", handleDeviceControl);
  
  // API маршруты
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/wifi-scan", HTTP_GET, handleApiWifiScan);
  server.on("/api/connected-devices", HTTP_GET, handleApiConnectedDevices);
  server.on("/api/device-info", HTTP_POST, handleApiDeviceInfo);
  server.on("/api/clear-settings", HTTP_POST, handleApiClearSettings);
  server.on("/api/restart", HTTP_POST, handleApiRestart);
  
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("HTTP server started");
}

// Фрагменты HTML страницы
String getHTMLHeader() {
  return R"=====(
<!DOCTYPE html>
<html>
<head>
    <title>ESP8266 Configuration</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }
        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .tab { overflow: hidden; border: 1px solid #ccc; background-color: #f1f1f1; border-radius: 5px 5px 0 0; }
        .tab button { background-color: inherit; float: left; border: none; outline: none; cursor: pointer; padding: 14px 16px; transition: 0.3s; font-size: 17px; }
        .tab button:hover { background-color: #ddd; }
        .tab button.active { background-color: #4CAF50; color: white; }
        .tabcontent { display: none; padding: 20px; border: 1px solid #ccc; border-top: none; border-radius: 0 0 5px 5px; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input, select, textarea { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; margin: 5px; }
        button:hover { background-color: #45a049; }
        table { width: 100%; border-collapse: collapse; margin-top: 10px; }
        th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
        th { background-color: #f2f2f2; }
        .modal { display: none; position: fixed; z-index: 1; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.4); }
        .modal-content { background-color: #fefefe; margin: 15% auto; padding: 20px; border: 1px solid #888; width: 80%; max-width: 500px; border-radius: 5px; }
        .close { color: #aaa; float: right; font-size: 28px; font-weight: bold; cursor: pointer; }
        .close:hover { color: black; }
    .tablinks{ color: black; }
    </style>
)=====";
}

String getJavaScript() {
  return R"=====(
    <script>
        function openTab(evt, tabName) {
            var i, tabcontent, tablinks;
            tabcontent = document.getElementsByClassName("tabcontent");
            for (i = 0; i < tabcontent.length; i++) {
                tabcontent[i].style.display = "none";
            }
            tablinks = document.getElementsByClassName("tablinks");
            for (i = 0; i < tablinks.length; i++) {
                tablinks[i].className = tablinks[i].className.replace(" active", "");
            }
            document.getElementById(tabName).style.display = "block";
            evt.currentTarget.className += " active";
            
            if (tabName === 'Status') {
                loadConnectedDevices();
            } else if (tabName === 'APSettings') {
                loadAPSettings();
            } else if (tabName === 'DeviceConfig') {
                loadDeviceConfig();
            } else if (tabName === 'DeviceControl') {
                loadDeviceControl();
            }
        }

        function loadConnectedDevices() {
            fetch('/api/connected-devices')
                .then(response => response.json())
                .then(data => {
                    let table = '<table><tr><th>IP Адрес</th><th>MAC Адрес</th><th>Имя устройства</th><th>Комментарий</th><th>Действия</th></tr>';
                    data.devices.forEach(device => {
                        table += `<tr>
                            <td>${device.ip}</td>
                            <td>${device.mac}</td>
                            <td>${device.device_name || ''}</td>
                            <td>${device.device_comment || ''}</td>
                            <td><button onclick="showDeviceInfo('${device.mac}', '${device.device_name || ''}', '${device.device_comment || ''}')">Информация об устройстве</button></td>
                        </tr>`;
                    });
                    table += '</table>';
                    document.getElementById('connectedDevicesTable').innerHTML = table;
                });
        }

        function showDeviceInfo(mac, name, comment) {
            document.getElementById('modalDeviceMac').value = mac;
            document.getElementById('modalDeviceName').value = name || '';
            document.getElementById('modalDeviceComment').value = comment || '';
            document.getElementById('deviceInfoModal').style.display = 'block';
        }

        function closeModal() {
            document.getElementById('deviceInfoModal').style.display = 'none';
        }

        function saveDeviceInfo() {
            const mac = document.getElementById('modalDeviceMac').value;
            const name = document.getElementById('modalDeviceName').value;
            const comment = document.getElementById('modalDeviceComment').value;
            
            fetch('/api/device-info', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ mac: mac, device_name: name, device_comment: comment })
            }).then(() => {
                closeModal();
                loadConnectedDevices();
            });
        }

        function loadAPSettings() {
            fetch('/api/settings')
                .then(response => response.json())
                .then(settings => {
                    document.getElementById('apSsid').value = settings.ap_ssid || '';
                    document.getElementById('apPassword').value = settings.ap_password || '';
                    
                    const subnetSelect = document.getElementById('subnet');
                    subnetSelect.innerHTML = '';
                    for (let i = 1; i <= 255; i++) {
                        const option = document.createElement('option');
                        option.value = i;
                        option.textContent = '192.168.' + i + '.1';
                        if (i === (settings.subnet || 4)) {
                            option.selected = true;
                        }
                        subnetSelect.appendChild(option);
                    }
                });
        }

        function saveAPSettings() {
            const settings = {
                ap_ssid: document.getElementById('apSsid').value,
                ap_password: document.getElementById('apPassword').value,
                subnet: parseInt(document.getElementById('subnet').value)
            };
            
            fetch('/api/settings', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(settings)
            }).then(() => alert('Настройки точки доступа сохранены'));
        }

        function scanWiFi() {
            fetch('/api/wifi-scan')
                .then(response => response.json())
                .then(data => {
                    let networks = '<h3>Доступные сети:</h3><ul>';
                    data.networks.forEach(network => {
                        networks += `<li>${network.ssid} (Сигнал: ${network.rssi}dBm)</li>`;
                    });
                    networks += '</ul>';
                    document.getElementById('wifiNetworks').innerHTML = networks;
                });
        }

        function loadDeviceConfig() {
            fetch('/api/settings')
                .then(response => response.json())
                .then(settings => {
                    document.getElementById('deviceName').value = settings.device_name || '';
                    document.getElementById('deviceComment').value = settings.device_comment || '';
                });
        }

        function saveDeviceConfig() {
            const config = {
                device_name: document.getElementById('deviceName').value,
                device_comment: document.getElementById('deviceComment').value
            };
            
            fetch('/api/settings', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            }).then(() => alert('Настройки устройства сохранены'));
        }

        function loadDeviceControl() {
            fetch('/api/settings')
                .then(response => response.json())
                .then(settings => {
                    document.getElementById('apModeEnabled').checked = settings.ap_mode_enabled || false;
                    document.getElementById('clientModeEnabled').checked = settings.client_mode_enabled || false;
                });
        }

        function saveDeviceControl() {
            const control = {
                ap_mode_enabled: document.getElementById('apModeEnabled').checked,
                client_mode_enabled: document.getElementById('clientModeEnabled').checked
            };
            
            fetch('/api/settings', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(control)
            }).then(() => alert('Настройки управления сохранены'));
        }

        function clearSettings() {
            if (confirm('Вы уверены, что хотите очистить все настройки?')) {
                fetch('/api/clear-settings', { method: 'POST' })
                    .then(() => alert('Настройки очищены'));
            }
        }

        function restartDevice() {
            if (confirm('Перезагрузить устройство?')) {
                fetch('/api/restart', { method: 'POST' });
            }
        }

        document.addEventListener('DOMContentLoaded', function() {
            loadConnectedDevices();
            loadAPSettings();
        });
    </script>
)=====";
}

String getHTMLBodyStart() {
  return R"=====(
</head>
<body>
    <div class="container">
        <h1>ESP8266 Configuration</h1>
        
        <div class="tab">
            <button class="tablinks active" onclick="openTab(event, 'Status')">Статус</button>
            <button class="tablinks" onclick="openTab(event, 'APSettings')">Настройки точки доступа</button>
            <button class="tablinks" onclick="openTab(event, 'WiFiScan')">Сканирование WiFi сетей</button>
            <button class="tablinks" onclick="openTab(event, 'DeviceConfig')">Настройки устройства</button>
            <button class="tablinks" onclick="openTab(event, 'DeviceControl')">Управление устройством</button>
        </div>
)=====";
}

String getStatusTab() {
  return R"=====(
        <div id="Status" class="tabcontent" style="display: block;">
            <h2>Список подключенных устройств</h2>
            <div id="connectedDevicesTable"></div>
        </div>
)=====";
}

String getAPSettingsTab() {
  return R"=====(
        <div id="APSettings" class="tabcontent">
            <h2>Настройки точки доступа</h2>
            <div class="form-group">
                <label for="apSsid">Имя точки доступа (SSID):</label>
                <input type="text" id="apSsid">
            </div>
            <div class="form-group">
                <label for="apPassword">Пароль точки доступа:</label>
                <input type="password" id="apPassword">
            </div>
            <div class="form-group">
                <label for="subnet">Подсеть (192.168.XXX.1):</label>
                <select id="subnet"></select>
            </div>
            <button onclick="saveAPSettings()">Сохранить настройки AP</button>
        </div>
)=====";
}

String getWiFiScanTab() {
  return R"=====(
        <div id="WiFiScan" class="tabcontent">
            <h2>Сканирование WiFi сетей</h2>
            <button onclick="scanWiFi()">Сканировать сети</button>
            <div id="wifiNetworks"></div>
        </div>
)=====";
}

String getDeviceConfigTab() {
  return R"=====(
        <div id="DeviceConfig" class="tabcontent">
            <h2>Настройки устройства</h2>
            <div class="form-group">
                <label for="deviceName">Имя устройства:</label>
                <input type="text" id="deviceName">
            </div>
            <div class="form-group">
                <label for="deviceComment">Комментарий для устройства:</label>
                <textarea id="deviceComment"></textarea>
            </div>
            <button onclick="saveDeviceConfig()">Сохранить настройки устройства</button>
        </div>
)=====";
}

String getDeviceControlTab() {
  return R"=====(
        <div id="DeviceControl" class="tabcontent">
            <h2>Управление устройством</h2>
            <div class="form-group">
                <label>
                    <input type="checkbox" id="apModeEnabled">
                    Режим точки доступа
                </label>
            </div>
            <div class="form-group">
                <label>
                    <input type="checkbox" id="clientModeEnabled">
                    Режим клиента WiFi
                </label>
            </div>
            <button onclick="saveDeviceControl()">Сохранить настройки</button>
            <button onclick="clearSettings()" style="background-color: #f44336;">Очистить настройки</button>
            <button onclick="restartDevice()" style="background-color: #ff9800;">Перезагрузить устройство</button>
        </div>
)=====";
}

String getModalWindow() {
  return R"=====(
        <!-- Модальное окно для информации об устройстве -->
        <div id="deviceInfoModal" class="modal">
            <div class="modal-content">
                <span class="close" onclick="closeModal()">&times;</span>
                <h2>Информация об устройстве</h2>
                <div class="form-group">
                    <label for="modalDeviceName">Имя устройства:</label>
                    <input type="text" id="modalDeviceName">
                </div>
                <div class="form-group">
                    <label for="modalDeviceComment">Комментарий:</label>
                    <textarea id="modalDeviceComment"></textarea>
                </div>
                <input type="hidden" id="modalDeviceMac">
                <button onclick="saveDeviceInfo()">Сохранить</button>
            </div>
        </div>
    </div>
</body>
</html>
)=====";
}

void handleRoot() {
  String html = getHTMLHeader();
  html += getJavaScript();
  html += getHTMLBodyStart();
  html += getStatusTab();
  html += getAPSettingsTab();
  html += getWiFiScanTab();
  html += getDeviceConfigTab();
  html += getDeviceControlTab();
  html += getModalWindow();
  
  server.send(200, "text/html", html);
}

void handleSettings() { handleRoot(); }
void handleWifiScan() { handleRoot(); }
void handleDeviceConfig() { handleRoot(); }
void handleDeviceControl() { handleRoot(); }

void handleGetSettings() {
  DynamicJsonDocument doc(1024);
  doc["ap_ssid"] = settings.ap_ssid;
  doc["ap_password"] = settings.ap_password;
  doc["device_name"] = settings.device_name;
  doc["device_comment"] = settings.device_comment;
  doc["subnet"] = settings.subnet;
  doc["ap_mode_enabled"] = settings.ap_mode_enabled;
  doc["client_mode_enabled"] = settings.client_mode_enabled;
  
  String response;
  serializeJson(doc, response);
  
  server.send(200, "application/json", response);
}

void handlePostSettings() {
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

void handleApiWifiScan() {
  int n = WiFi.scanNetworks();
  DynamicJsonDocument doc(2048);
  JsonArray networks = doc.createNestedArray("networks");
  
  for (int i = 0; i < n; i++) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["encryption"] = WiFi.encryptionType(i);
  }
  
  String response;
  serializeJson(doc, response);
  
  server.send(200, "application/json", response);
}

void handleApiConnectedDevices() {
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

void handleApiDeviceInfo() {
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

void handleApiClearSettings() {
  // Сброс настроек к значениям по умолчанию
  strlcpy(settings.ap_ssid, "VR_APP_ESP", sizeof(settings.ap_ssid));
  strlcpy(settings.ap_password, "12345678", sizeof(settings.ap_password));
  strlcpy(settings.device_name, "ESP8266_Device", sizeof(settings.device_name));
  strlcpy(settings.device_comment, "Default Comment", sizeof(settings.device_comment));
  settings.subnet = 4;
  settings.ap_mode_enabled = true;
  settings.client_mode_enabled = false;
  
  saveSettings();
  
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiRestart() {
  server.send(200, "application/json", "{\"status\":\"restarting\"}");
  delay(1000);
  ESP.restart();
}

void handleNotFound() {
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

void loadSettings() {
  EEPROM.get(SETTINGS_ADDR, settings);
  
  // Проверка валидности загруженных настроек
  if (settings.subnet < 1 || settings.subnet > 255) {
    settings.subnet = 4;
  }
}

void saveSettings() {
  EEPROM.put(SETTINGS_ADDR, settings);
  EEPROM.commit();
}

void updateConnectedDevices() {
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

String macToString(uint8_t* mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}
