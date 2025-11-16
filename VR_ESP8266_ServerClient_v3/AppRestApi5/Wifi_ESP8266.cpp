#include "Wifi_ESP8266.h"

WiFiManager wifiManager;

WiFiManager::WiFiManager() : server(80) {
  // Конструктор
}

void WiFiManager::begin() {
  // Используем настройки по умолчанию
  begin("VR_APP_ESP", "12345678", 4);
}

void WiFiManager::begin(const char* ap_ssid, const char* ap_password) {
  // Используем подсеть по умолчанию
  begin(ap_ssid, ap_password, 4);
}

void WiFiManager::begin(const char* ap_ssid, const char* ap_password, int subnet) {
  Serial.begin(115200);
  
  // Инициализация EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Загрузка настроек из EEPROM
  loadSettings();
  
  // Обновляем настройки переданными параметрами
  strlcpy(settings.ap_ssid, ap_ssid, sizeof(settings.ap_ssid));
  strlcpy(settings.ap_password, ap_password, sizeof(settings.ap_password));
  settings.subnet = subnet;
  
  // Сохраняем обновленные настройки
  saveSettings();
  
  // Запуск WiFi в зависимости от настроек
  setupWiFi();
  
  // Настройка веб-сервера
  setupWebServer();
  
  Serial.println("WiFi Manager started");
  Serial.println("AP SSID: " + String(settings.ap_ssid));
  Serial.println("Subnet: 192.168." + String(settings.subnet) + ".1");
}

void WiFiManager::handleClient() {
  server.handleClient();
}

void WiFiManager::update() {
  // Обновление списка подключенных устройств каждые 5 секунд
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 5000) {
    updateConnectedDevices();
    lastUpdate = millis();
  }
}

void WiFiManager::setupWiFi() {
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
  
  // Подключаемся к WiFi сети если указаны учетные данные
  if (settings.client_mode_enabled && strlen(settings.sta_ssid) > 0) {
    connectToWiFi();
  }
}

// Остальной код остается без изменений...
void WiFiManager::connectToWiFi() {
  if (strlen(settings.sta_ssid) > 0) {
    Serial.println("Connecting to WiFi: " + String(settings.sta_ssid));
    WiFi.begin(settings.sta_ssid, settings.sta_password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!");
      Serial.println("IP address: " + WiFi.localIP().toString());
    } else {
      Serial.println("\nFailed to connect to WiFi");
    }
  }
}

void WiFiManager::disconnectFromWiFi() {
  WiFi.disconnect();
  delay(1000);
  Serial.println("Disconnected from WiFi");
}

void WiFiManager::setupWebServer() {
  // Основные маршруты
  server.on("/", std::bind(&WiFiManager::handleRoot, this));
  server.on("/settings", std::bind(&WiFiManager::handleRoot, this));
  server.on("/wifi-scan", std::bind(&WiFiManager::handleRoot, this));
  server.on("/device-config", std::bind(&WiFiManager::handleRoot, this));
  server.on("/device-control", std::bind(&WiFiManager::handleRoot, this));
  
  // API маршруты
  server.on("/api/settings", HTTP_GET, std::bind(&WiFiManager::handleGetSettings, this));
  server.on("/api/settings", HTTP_POST, std::bind(&WiFiManager::handlePostSettings, this));
  server.on("/api/wifi-scan", HTTP_GET, std::bind(&WiFiManager::handleApiWifiScan, this));
  server.on("/api/connected-devices", HTTP_GET, std::bind(&WiFiManager::handleApiConnectedDevices, this));
  server.on("/api/device-info", HTTP_POST, std::bind(&WiFiManager::handleApiDeviceInfo, this));
  server.on("/api/clear-settings", HTTP_POST, std::bind(&WiFiManager::handleApiClearSettings, this));
  server.on("/api/restart", HTTP_POST, std::bind(&WiFiManager::handleApiRestart, this));
  server.on("/api/wifi-connect", HTTP_POST, std::bind(&WiFiManager::handleApiWifiConnect, this));
  server.on("/api/wifi-disconnect", HTTP_POST, std::bind(&WiFiManager::handleApiWifiDisconnect, this));
  server.on("/api/wifi-status", HTTP_GET, std::bind(&WiFiManager::handleApiWifiStatus, this));
  
  server.onNotFound(std::bind(&WiFiManager::handleNotFound, this));
  
  server.begin();
  Serial.println("HTTP server started");
}

// Все остальные методы остаются без изменений...
String WiFiManager::getHTMLHeader() {
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
        .wifi-network { border: 1px solid #ddd; padding: 10px; margin: 5px 0; border-radius: 4px; }
        .wifi-connected { background-color: #e8f5e8; }
        .wifi-disconnected { background-color: #f5f5f5; }
        .tablinks{ color: black; }
        .connection-status { padding: 10px; border-radius: 4px; margin: 10px 0; }
        .connected { background-color: #d4edda; color: #155724; }
        .disconnected { background-color: #f8d7da; color: #721c24; }
    </style>
)=====";
}

String WiFiManager::getJavaScript() {
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
            } else if (tabName === 'WiFiScan') {
                loadWiFiStatus();
                scanWiFi();
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
                    let networksHTML = '<h3>Доступные сети:</h3>';
                    data.networks.forEach(network => {
                        const encryption = network.encryption === 7 ? 'Open' : 'Secured';
                        networksHTML += `
                            <div class="wifi-network">
                                <strong>${network.ssid}</strong><br>
                                Сигнал: ${network.rssi}dBm | Защита: ${encryption}<br>
                                <input type="password" id="password_${network.ssid.replace(/[^a-zA-Z0-9]/g, '_')}" placeholder="Пароль" style="width: 200px; margin: 5px 0;">
                                <button onclick="connectToNetwork('${network.ssid}', ${network.encryption})">Подключиться</button>
                            </div>
                        `;
                    });
                    document.getElementById('wifiNetworks').innerHTML = networksHTML;
                });
        }

        function connectToNetwork(ssid, encryption) {
            const passwordId = 'password_' + ssid.replace(/[^a-zA-Z0-9]/g, '_');
            const password = document.getElementById(passwordId).value;
            
            if (encryption !== 7 && !password) {
                alert('Для защищенной сети необходим пароль');
                return;
            }
            
            const connectionData = {
                ssid: ssid,
                password: password
            };
            
            fetch('/api/wifi-connect', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(connectionData)
            })
            .then(response => response.json())
            .then(data => {
                alert('Подключение к сети ' + ssid + ' выполнено');
                loadWiFiStatus();
            })
            .catch(error => {
                alert('Ошибка подключения к сети');
            });
        }

        function disconnectFromWiFi() {
            if (confirm('Отключиться от текущей WiFi сети?')) {
                fetch('/api/wifi-disconnect', { method: 'POST' })
                    .then(response => response.json())
                    .then(data => {
                        alert('Отключено от WiFi сети');
                        loadWiFiStatus();
                        scanWiFi();
                    });
            }
        }

        function loadWiFiStatus() {
            fetch('/api/wifi-status')
                .then(response => response.json())
                .then(data => {
                    let statusHTML = '';
                    if (data.connected) {
                        statusHTML = `
                            <div class="connection-status connected">
                                <strong>Подключено к WiFi</strong><br>
                                Сеть: ${data.ssid}<br>
                                IP: ${data.ip}<br>
                                Сигнал: ${data.rssi}dBm
                            </div>
                            <button onclick="disconnectFromWiFi()" style="background-color: #f44336;">Отключиться от точки доступа</button>
                        `;
                    } else {
                        statusHTML = `
                            <div class="connection-status disconnected">
                                <strong>Не подключено к WiFi</strong>
                            </div>
                        `;
                    }
                    document.getElementById('wifiStatus').innerHTML = statusHTML;
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

String WiFiManager::getHTMLBodyStart() {
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

String WiFiManager::getStatusTab() {
  return R"=====(
        <div id="Status" class="tabcontent" style="display: block;">
            <h2>Список подключенных устройств</h2>
            <div id="connectedDevicesTable"></div>
        </div>
)=====";
}

String WiFiManager::getAPSettingsTab() {
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

String WiFiManager::getWiFiScanTab() {
  return R"=====(
        <div id="WiFiScan" class="tabcontent">
            <h2>Сканирование WiFi сетей</h2>
            <div id="wifiStatus"></div>
            <button onclick="scanWiFi()">Сканировать сети</button>
            <button onclick="loadWiFiStatus()">Обновить статус</button>
            <div id="wifiNetworks"></div>
        </div>
)=====";
}

String WiFiManager::getDeviceConfigTab() {
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

String WiFiManager::getDeviceControlTab() {
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

String WiFiManager::getModalWindow() {
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

void WiFiManager::handleApiWifiScan() {
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

void WiFiManager::handleApiWifiConnect() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, server.arg("plain"));
    
    String ssid = doc["ssid"];
    String password = doc["password"];
    
    // Сохраняем учетные данные
    strlcpy(settings.sta_ssid, ssid.c_str(), sizeof(settings.sta_ssid));
    strlcpy(settings.sta_password, password.c_str(), sizeof(settings.sta_password));
    settings.client_mode_enabled = true;
    saveSettings();
    
    // Подключаемся к WiFi
    connectToWiFi();
    
    server.send(200, "application/json", "{\"status\":\"connecting\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"No data\"}");
  }
}

void WiFiManager::handleApiWifiDisconnect() {
  disconnectFromWiFi();
  
  // Очищаем сохраненные учетные данные
  memset(settings.sta_ssid, 0, sizeof(settings.sta_ssid));
  memset(settings.sta_password, 0, sizeof(settings.sta_password));
  settings.client_mode_enabled = false;
  saveSettings();
  
  server.send(200, "application/json", "{\"status\":\"disconnected\"}");
}

void WiFiManager::handleApiWifiStatus() {
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
  
  server.send(200, "application/json", response);
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
  // Сброс настроек к значениям по умолчанию
  strlcpy(settings.ap_ssid, "VR_APP_ESP", sizeof(settings.ap_ssid));
  strlcpy(settings.ap_password, "12345678", sizeof(settings.ap_password));
  strlcpy(settings.device_name, "ESP8266_Device", sizeof(settings.device_name));
  strlcpy(settings.device_comment, "Default Comment", sizeof(settings.device_comment));
  settings.subnet = 4;
  settings.ap_mode_enabled = true;
  settings.client_mode_enabled = false;
  memset(settings.sta_ssid, 0, sizeof(settings.sta_ssid));
  memset(settings.sta_password, 0, sizeof(settings.sta_password));
  
  saveSettings();
  
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

void WiFiManager::loadSettings() {
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

String WiFiManager::macToString(uint8_t* mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}
