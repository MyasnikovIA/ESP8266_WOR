#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

void sendHTMLChunked();

// Структура для хранения настроек
struct Settings {
  char ap_ssid[32] = "VR_APP_ESP";
  char ap_password[32] = "12345678";
  char sta_ssid[32] = "";
  char sta_password[32] = "";
  char device_name[32] = "ESP8266_Device";
  char device_comment[64] = "";
  char json_attributes[512] = "{}";
  int subnet = 4;
  bool ap_enabled = true;
  bool sta_enabled = false;
  bool settings_valid = false;
};

// Структура для хранения информации о подключенных устройствах
struct DeviceInfo {
  char mac[18]; // MAC адрес устройства
  char name[32]; // Имя устройства
  char comment[128]; // Комментарий к устройству
  bool valid = false;
};

Settings settings;
ESP8266WebServer server(80);
WiFiClient wifiClient;

// Адреса EEPROM
const int EEPROM_SIZE = 4096;
const int SETTINGS_ADDR = 0;
const int DEVICE_INFO_ADDR = 512; // Адрес начала хранения информации об устройствах
const int MAX_DEVICES = 20; // Максимальное количество устройств для хранения информации

// Переменные для сканирования WiFi
String wifiNetworks = "[]";
unsigned long lastScan = 0;
const unsigned long SCAN_INTERVAL = 10000;

// Переменные для клиента WiFi
unsigned long wifiConnectTime = 0;
bool wifiConnected = false;
bool dataSent = false;

// Флаги для управления WiFi
bool needsWiFiRestart = false;
bool needsAPRestart = false;

// Прототипы функций
void handleGetSettings();
void handlePostSettings();
void handleWiFiScan();
void handleWiFiConnect();
void handleWiFiDisconnect();
void handleGetConnectedDevices();
void handleUpdateDevice();
void handleClearSettings();
void handleDeviceRegister();
void handleDeviceUpdate();
void handleGetConnectedDevicesAPI();
void handleGetWiFiStatus();
void handleGetDeviceSettingsAPI();
void scanWiFiNetworks();
String getConnectedDevicesJSON();
String mac2str(const uint8_t* mac);
void loadSettings();
void saveSettings();
void setupWiFi();
void handleWiFiClient();
void sendDeviceDataToGateway();
void setupWebServer();
void applySettingsChanges();
void saveDeviceInfo(const String& mac, const String& name, const String& comment);
DeviceInfo loadDeviceInfo(const String& mac);
void clearDeviceInfo();
void updateDeviceInfo(const String& mac, const String& name, const String& comment);

void setup() {
  Serial.begin(115200);
  
  // Инициализация EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadSettings();
  
  // Настройка WiFi
  setupWiFi();
  
  // Настройка веб-сервера
  setupWebServer();
  
  Serial.println("Device started");
}

void loop() {
  server.handleClient();
  
  // Обработка подключения к WiFi клиенту
  handleWiFiClient();
  
  // Применение изменений настроек WiFi
  if (needsWiFiRestart) {
    needsWiFiRestart = false;
    setupWiFi();
  }
  
  // Периодическое сканирование WiFi сетей
  if (millis() - lastScan > SCAN_INTERVAL) {
    scanWiFiNetworks();
    lastScan = millis();
  }
}

void setupWiFi() {
  WiFi.mode(WIFI_AP_STA);
  
  // Настройка точки доступа
  if (settings.ap_enabled) {
    String ap_ip = "192.168." + String(settings.subnet) + ".1";
    IPAddress local_ip;
    local_ip.fromString(ap_ip);
    IPAddress gateway(local_ip);
    IPAddress subnet(255, 255, 255, 0);
    
    WiFi.softAPConfig(local_ip, gateway, subnet);
    WiFi.softAP(settings.ap_ssid, settings.ap_password);
    Serial.println("AP started: " + String(settings.ap_ssid));
    Serial.println("AP IP: " + ap_ip);
  } else {
    WiFi.softAPdisconnect(true);
    Serial.println("AP disabled");
  }
  
  // Подключение к WiFi сети
  if (settings.sta_enabled && strlen(settings.sta_ssid) > 0) {
    WiFi.begin(settings.sta_ssid, settings.sta_password);
    Serial.println("Connecting to WiFi: " + String(settings.sta_ssid));
    wifiConnectTime = millis();
    wifiConnected = false;
    dataSent = false;
  } else {
    WiFi.disconnect();
    wifiConnected = false;
    dataSent = false;
    Serial.println("STA disabled");
  }
}

void handleWiFiClient() {
  if (settings.sta_enabled && strlen(settings.sta_ssid) > 0) {
    if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
      wifiConnected = true;
      Serial.println("Connected to WiFi!");
      Serial.println("IP address: " + WiFi.localIP().toString());
      
      // Отправка данных через 3 секунды
      delay(3000);
      sendDeviceDataToGateway();
    }
    
    // Проверка таймаута отправки данных (5 секунд)
    if (wifiConnected && !dataSent && millis() - wifiConnectTime > 8000) {
      dataSent = true;
      Serial.println("Gateway not responding, data not sent");
    }
    
    if (WiFi.status() != WL_CONNECTED && wifiConnected) {
      wifiConnected = false;
      dataSent = false;
      Serial.println("Disconnected from WiFi");
    }
  }
}

void sendDeviceDataToGateway() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String gateway_ip = "http://192.168." + String(settings.subnet) + ".1/device/register";
    
    http.begin(wifiClient, gateway_ip);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Access-Control-Allow-Origin", "*");
    
    // Создание JSON с данными устройства
    DynamicJsonDocument doc(1024);
    doc["device_name"] = settings.device_name;
    doc["device_comment"] = settings.device_comment;
    
    JsonObject attributes = doc.createNestedObject("attributes");
    DynamicJsonDocument attrDoc(512);
    deserializeJson(attrDoc, settings.json_attributes);
    attributes.set(attrDoc.as<JsonObject>());
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpCode = http.POST(jsonString);
    
    if (httpCode > 0) {
      Serial.println("Data sent to gateway: " + String(httpCode));
      dataSent = true;
    } else {
      Serial.println("Failed to send data to gateway");
    }
    
    http.end();
  }
}

void setupWebServer() {
  // Главная страница
  server.on("/", HTTP_GET, []() {
    sendHTMLChunked();
  });
  
  // API endpoints
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);
  server.on("/api/wifi/connect", HTTP_POST, handleWiFiConnect);
  server.on("/api/wifi/disconnect", HTTP_POST, handleWiFiDisconnect);
  server.on("/api/wifi/status", HTTP_GET, handleGetWiFiStatus);
  server.on("/api/connected-devices", HTTP_GET, handleGetConnectedDevices);
  server.on("/api/device/update", HTTP_POST, handleUpdateDevice);
  server.on("/api/clear-settings", HTTP_POST, handleClearSettings);
  server.on("/api/device-settings", HTTP_GET, handleGetDeviceSettingsAPI);
  
  server.on("/device/register", HTTP_POST, handleDeviceRegister);
  server.on("/device/update", HTTP_POST, handleDeviceUpdate);
  server.on("/connected-devices", HTTP_GET, handleGetConnectedDevicesAPI);
  
  // CORS для кросс-доменных запросов
  server.onNotFound([]() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(404, "text/plain", "Not Found");
  });
  
  server.begin();
  Serial.println("HTTP server started");
}

void sendHTMLChunked() {
  WiFiClient client = server.client();
  
  // Send HTTP headers
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  // Send HTML in chunks
  client.print(R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <title>ESP8266 WiFi Gateway</title>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <script>
            // Глобальные переменные
            let attributes = [];
            let modalAttributes = [];
    
            // Основные функции
      function openTab(tabName) {
        const tabs = document.getElementsByClassName('tab-content');
        for (let tab of tabs) {
          tab.classList.remove('active');
        }
        
        const buttons = document.getElementsByClassName('tab-button');
        for (let button of buttons) {
          button.classList.remove('active');
        }
        
        document.getElementById(tabName).classList.add('active');
        event.currentTarget.classList.add('active');
        
        if (tabName === 'wifi-scan') {
          loadWiFiStatus();
        } 
        if (tabName === 'status') {
          window.loadConnectedDevices2();
        }
        if (tabName === 'device-settings') {
          loadDeviceSettings();
        }
      }
    
            function populateSubnetSelect() {
                const select = document.getElementById('subnet');
                for (let i = 1; i <= 255; i++) {
                    const option = document.createElement('option');
                    option.value = i;
                    option.textContent = i;
                    select.appendChild(option);
                }
            }
            // Функции загрузки данных
            function loadSettings() {
                fetch('/api/settings')
                    .then(response => response.json())
                    .then(data => {
                        document.getElementById('ap_ssid').value = data.ap_ssid || '';
                        document.getElementById('ap_password').value = data.ap_password || '';
                        document.getElementById('device_name').value = data.device_name || '';
                        document.getElementById('device_comment').value = data.device_comment || '';
                        document.getElementById('subnet').value = data.subnet || 4;
                        document.getElementById('ap_enabled').checked = data.ap_enabled || false;
                        document.getElementById('sta_enabled').checked = data.sta_enabled || false;
                        
                        if (data.json_attributes && data.json_attributes !== '{}') {
                            try {
                                const attrs = JSON.parse(data.json_attributes);
                                attributes = [];
                                const container = document.getElementById('attributesContainer');
                                container.innerHTML = '';
                                
                                for (const [key, value] of Object.entries(attrs)) {
                                    attributes.push({key, value});
                                    addAttributeField(key, value);
                                }
                            } catch (e) {
                                console.error('Error parsing attributes:', e);
                            }
                        }
                    })
                    .catch(error => console.error('Error loading settings:', error));
            }
    
            function loadWiFiStatus() {
                fetch('/api/wifi/status')
                    .then(response => response.json())
                    .then(data => {
                        const statusDiv = document.getElementById('wifiStatus');
                        const disconnectBtn = document.getElementById('disconnectBtn');
                        
                        if (data.status === 'connected') {
                            statusDiv.innerHTML = `
                                <div class="wifi-status-connected">Status: Connected</div>
                                <div class="status-info">
                                    <p><strong>SSID:</strong> ${data.ssid || 'N/A'}</p>
                                    <p><strong>IP Address:</strong> ${data.ip || 'N/A'}</p>
                                    <p><strong>Gateway:</strong> ${data.gateway || 'N/A'}</p>
                                    <p><strong>Subnet Mask:</strong> ${data.subnet || 'N/A'}</p>
                                    <p><strong>MAC Address:</strong> ${data.mac || 'N/A'}</p>
                                    <p><strong>Signal Strength:</strong> ${data.rssi || 'N/A'} dBm</p>
                                </div>
                            `;
                            disconnectBtn.style.display = 'inline-block';
                        } else {
                            statusDiv.innerHTML = `
                                <div class="wifi-status-disconnected">Status: Disconnected</div>
                                <p>Not connected to any WiFi network</p>
                            `;
                            disconnectBtn.style.display = 'none';
                        }
                    })
                    .catch(error => {
                        console.error('Error loading WiFi status:', error);
                        document.getElementById('wifiStatus').innerHTML = '<p>Error loading WiFi status</p>';
                    });
            }
    
            function scanWiFi() {
                fetch('/api/wifi/scan')
                    .then(response => {
                        if (!response.ok) {
                            throw new Error('Network response was not ok');
                        }
                        return response.json();
                    })
                    .then(networks => {
                        const container = document.getElementById('wifiNetworks');
                        container.innerHTML = '';
                        
                        if (Array.isArray(networks)) {
                            networks.forEach(network => {
                                if (network && network.ssid) {
                                    const div = document.createElement('div');
                                    div.className = 'wifi-network';
                                    div.innerHTML = '<strong>' + network.ssid + '</strong> (RSSI: ' + network.rssi + ', ' + network.encryption + ')';
                                    div.onclick = function() {
                                        document.getElementById('wifi_ssid').value = network.ssid;
                                    };
                                    container.appendChild(div);
                                }
                            });
                        }
                    })
                    .catch(error => {
                        console.error('Error scanning WiFi:', error);
                        alert('Error scanning WiFi: ' + error.message);
                    });
            }
)rawliteral");

  client.print(R"rawliteral(
            function disconnectWiFi() {
                if (confirm('Are you sure you want to disconnect from WiFi?')) {
                    fetch('/api/wifi/disconnect', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'}
                    })
                    .then(response => response.json())
                    .then(data => {
                        alert('WiFi disconnected successfully!');
                        setTimeout(loadWiFiStatus, 1000);
                    })
                    .catch(error => {
                        alert('Error disconnecting WiFi: ' + error);
                    });
                }
            }

        // Функции для атрибутов
        function addAttribute(key = '', value = '') {
            const container = document.getElementById('attributesContainer');
            const index = attributes.length;
            
            attributes.push({key, value});
            
            const row = document.createElement('div');
            row.className = 'attribute-row';
            row.innerHTML = `
                <input type="text" placeholder="Key" value="${key}" onchange="updateAttribute(${index}, 'key', this.value)">
                <input type="text" placeholder="Value" value="${value}" onchange="updateAttribute(${index}, 'value', this.value)">
                <button type="button" onclick="removeAttributeRow(${index})">Remove</button>
            `;
            
            container.appendChild(row);
        }


        function addAttributeField(key, value) {
            const container = document.getElementById('attributesContainer');
            const index = attributes.length - 1;
            
            const row = document.createElement('div');
            row.className = 'attribute-row';
            row.innerHTML = `
                <input type="text" placeholder="Key" value="${key}" onchange="updateAttribute(${index}, 'key', this.value)">
                <input type="text" placeholder="Value" value="${value}" onchange="updateAttribute(${index}, 'value', this.value)">
                <button type="button" onclick="removeAttributeRow(${index})">Remove</button>
            `;
            
            container.appendChild(row);
        }

        function updateAttribute(index, field, value) {
            if (attributes[index]) {
                attributes[index][field] = value;
            }
        }

        function updateModalAttribute(index, field, value) {
            if (modalAttributes[index]) {
                modalAttributes[index][field] = value;
            }
        }

        function removeAttributeRow(index) {
            attributes.splice(index, 1);
            renderAttributes();
        }

        function removeModalAttribute(index) {
            modalAttributes.splice(index, 1);
        }

        function renderAttributes() {
            const container = document.getElementById('attributesContainer');
            container.innerHTML = '';
            
            attributes.forEach((attr, index) => {
                addAttributeField(attr.key, attr.value);
            });
        }

        let devicesConnect = [];
        // Функции управления
        function clearSettings() {
            if (confirm('Are you sure you want to clear all settings?')) {
                fetch('/api/clear-settings', {method: 'POST'})
                    .then(response => response.json())
                    .then(data => {
                        alert('All settings cleared! Device will restart.');
                        setTimeout(() => location.reload(), 2000);
                    })
                    .catch(error => {
                        alert('Error clearing settings: ' + error);
                    });
            }
        }

        function restartDevice() {
            if (confirm('Are you sure you want to restart the device?')) {
                alert('Restart functionality would be implemented here');
            }
        }

        function showDeviceInfo(ind) {
            const device = devicesConnect[ind];
            const ip = device.ip || ''; 
            const mac = device.mac || ''; 
            const name = device.name || ''; 
            const comment = device.comment || ''; 
            
            document.getElementById('modal_ip').value = ip;
            document.getElementById('modal_mac').value = mac;
            document.getElementById('modal_name').value = name || '';
            document.getElementById('modal_comment').value = comment || '';
            document.getElementById('deviceModal').style.display = 'block';
        }
)rawliteral");

  client.print(R"rawliteral(

        function loadConnectedDevices2() {
            fetch('/api/connected-devices')
                .then(response => response.json())
                .then(devices => {
                    const tbody = document.querySelector('#connectedDevicesTable tbody');
                    tbody.innerHTML = '';
                    
                    if (Array.isArray(devices)) {
                        devicesConnect = devices;
            for (let i = 0; i < devices.length; i++) {
              const device = devices[i];
              const row = document.createElement('tr');
              
              // Создаем ячейки безопасным способом
              const ipCell = document.createElement('td');
              ipCell.textContent = device.ip || '';
              
              const macCell = document.createElement('td');
              macCell.textContent = device.mac || '';
              
              const nameCell = document.createElement('td');
              nameCell.textContent = device.name || '';
              
              const commentCell = document.createElement('td');
              commentCell.textContent = device.comment || '';
              
              const actionCell = document.createElement('td');
              
              // Создаем кнопку через строку с динамической функцией
              actionCell.innerHTML = `<button onclick="showDeviceInfo(${i})">Device Info</button>`;
              
              // Добавляем ячейки в строку
              row.appendChild(ipCell);
              row.appendChild(macCell);
              row.appendChild(nameCell);
              row.appendChild(commentCell);
              row.appendChild(actionCell);
              
              tbody.appendChild(row);
            }
                    } else {
                       devicesConnect = [];
                    }
                })
                .catch(error => console.error('Error loading connected devices:', error));
        } 
        // Обработчики событий
        function setupEventListeners() {
            // AP Settings Form
            const apSettingsForm = document.getElementById('apSettingsForm');
            if (apSettingsForm) {
                apSettingsForm.addEventListener('submit', function(e) {
                    e.preventDefault();
                    
                    const formData = {
                        ap_ssid: document.getElementById('ap_ssid').value,
                        ap_password: document.getElementById('ap_password').value,
                        subnet: parseInt(document.getElementById('subnet').value),
                        ap_enabled: document.getElementById('ap_enabled').checked
                    };
                    
                    fetch('/api/settings', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify(formData)
                    })
                    .then(response => response.json())
                    .then(data => {
                        alert('AP settings saved successfully!');
                    })
                    .catch(error => {
                        alert('Error saving AP settings: ' + error);
                    });
                });
            }
            
            // WiFi Connect Form
            const wifiConnectForm = document.getElementById('wifiConnectForm');
            if (wifiConnectForm) {
                wifiConnectForm.addEventListener('submit', function(e) {
                    e.preventDefault();
                    
                    const formData = {
                        ssid: document.getElementById('wifi_ssid').value,
                        password: document.getElementById('wifi_password').value,
                        sta_enabled: document.getElementById('sta_enabled').checked
                    };
                    
                    fetch('/api/wifi/connect', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify(formData)
                    })
                    .then(response => response.json())
                    .then(data => {
                        alert('Connecting to WiFi... Please wait and refresh the status.');
                        setTimeout(loadWiFiStatus, 3000);
                    })
                    .catch(error => {
                        alert('Error connecting to WiFi: ' + error);
                    });
                });
            }
            
            // Device Settings Form
            const deviceSettingsForm = document.getElementById('deviceSettingsForm');
            if (deviceSettingsForm) {
                deviceSettingsForm.addEventListener('submit', function(e) {
                    e.preventDefault();
                    
                    const attributesObj = {};
                    attributes.forEach(attr => {
                        if (attr.key && attr.key.trim() !== '') {
                            attributesObj[attr.key] = attr.value;
                        }
                    });
                    
                    const formData = {
                        device_name: document.getElementById('device_name').value,
                        device_comment: document.getElementById('device_comment').value,
                        json_attributes: JSON.stringify(attributesObj)
                    };
                    
                    fetch('/api/settings', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify(formData)
                    })
                    .then(response => response.json())
                    .then(data => {
                        alert('Device settings saved successfully!');
                    })
                    .catch(error => {
                        alert('Error saving device settings: ' + error);
                    });
                });
            }
            
            // Device Info Form (Modal)
            const deviceInfoForm = document.getElementById('deviceInfoForm');
            if (deviceInfoForm) {
                deviceInfoForm.addEventListener('submit', function(e) {
                    e.preventDefault();
                    
                    const formData = {
                        ip: document.getElementById('modal_ip').value,
                        mac: document.getElementById('modal_mac').value,
                        name: document.getElementById('modal_name').value,
                        comment: document.getElementById('modal_comment').value
                    };
                    
                    console.log('Sending device update:', formData);
                    
                    fetch('/api/device/update', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify(formData)
                    })
                    .then(response => response.json())
                    .then(data => {
                        alert('Device information updated!');
                        document.getElementById('deviceModal').style.display = 'none';
                        window.loadConnectedDevices2();
                    })
                    .catch(error => {
                        alert('Error updating device information: ' + error);
                    });
                });
            }
            
            // Modal close button
            const closeBtn = document.querySelector('.close');
            if (closeBtn) {
                closeBtn.onclick = function() {
                    document.getElementById('deviceModal').style.display = 'none';
                }
            }
            
            // Close modal when clicking outside
            window.onclick = function(event) {
                const modal = document.getElementById('deviceModal');
                if (event.target == modal) {
                    modal.style.display = 'none';
                }
            }
        }

    // Функция для обновления настроек устройства
    function refreshDeviceSettings() {
      fetch('/api/device-settings')
        .then(response => {
          if (!response.ok) {
            throw new Error('Network response was not ok');
          }
          return response.json();
        })
        .then(data => {
          // Обновляем поля формы
          document.getElementById('device_name').value = data.device_name || '';
          document.getElementById('device_comment').value = data.device_comment || '';
          
          // Обновляем атрибуты
          if (data.system_attributes) {
            attributes = [];
            const container = document.getElementById('attributesContainer');
            container.innerHTML = '';
            
            for (const [key, value] of Object.entries(data.system_attributes)) {
              attributes.push({key, value});
              addAttributeField(key, value);
            }
            
            // Если атрибутов нет, добавляем пустое поле
            if (attributes.length === 0) {
              addAttribute();
            }
          }
          
          alert('Device settings refreshed successfully!');
        })
        .catch(error => {
          console.error('Error refreshing device settings:', error);
          alert('Error refreshing device settings: ' + error.message);
        });
    }

    // Функция для загрузки настроек устройства при открытии вкладки
    function loadDeviceSettings() {
      fetch('/api/device-settings')
        .then(response => response.json())
        .then(data => {
          document.getElementById('device_name').value = data.device_name || '';
          document.getElementById('device_comment').value = data.device_comment || '';
          
          if (data.system_attributes) {
            attributes = [];
            const container = document.getElementById('attributesContainer');
            container.innerHTML = '';
            
            for (const [key, value] of Object.entries(data.system_attributes)) {
              attributes.push({key, value});
              addAttributeField(key, value);
            }
          }
          
          // Если атрибутов нет, добавляем пустое поле
          if (attributes.length === 0) {
            addAttribute();
          }
        })
        .catch(error => console.error('Error loading device settings:', error));
    }


        // Инициализация при загрузке страницы
        document.addEventListener('DOMContentLoaded', function() {
            setupEventListeners();
            loadSettings();
            populateSubnetSelect();
            addAttribute();
            loadWiFiStatus();
        });
    </script>
</body>
</html>
)rawliteral");

  // Send HTML in chunks
  client.print(R"rawliteral(
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 20px;
            background-color: #f5f5f5;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        .tabs {
            display: flex;
            margin-bottom: 20px;
            border-bottom: 1px solid #ddd;
        }
        .tab-button {
            padding: 10px 20px;
            border: none;
            background: none;
            cursor: pointer;
            border-bottom: 2px solid transparent;
            color: black;
            font-weight: bold;
        }
        .tab-button.active {
            border-bottom-color: #007bff;
            color: #007bff;
        }
        .tab-content {
            display: none;
        }
        .tab-content.active {
            display: block;
        }
        .section {
            margin-bottom: 20px;
            padding: 15px;
            border: 1px solid #ddd;
            border-radius: 5px;
        }
        .black-header {
            color: black !important;
        }
        label {
            display: block;
            margin: 10px 0 5px;
            font-weight: bold;
        }
        input, textarea, select {
            width: 100%;
            padding: 8px;
            margin-bottom: 10px;
            border: 1px solid #ddd;
            border-radius: 4px;
            box-sizing: border-box;
        }
        button {
            padding: 10px 15px;
            background: #007bff;
            color: white;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            margin: 5px;
        }
        button:hover {
            background: #0056b3;
        }
        .disconnect-btn {
            background: #dc3545;
        }
        .disconnect-btn:hover {
            background: #c82333;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin-top: 10px;
        }
        th, td {
            padding: 8px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        th {
            background-color: #f8f9fa;
        }
        .attribute-row {
            display: flex;
            margin-bottom: 10px;
            gap: 10px;
        }
        .attribute-row input {
            flex: 1;
            margin-bottom: 0;
        }
        .attribute-row button {
            width: auto;
            background: #dc3545;
        }
        .modal {
            display: none;
            position: fixed;
            z-index: 1000;
            left: 0;
            top: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(0,0,0,0.5);
        }
        .modal-content {
            background-color: white;
            margin: 10% auto;
            padding: 20px;
            border-radius: 8px;
            width: 500px;
            max-width: 90%;
            position: relative;
        }
        .close {
            position: absolute;
            right: 15px;
            top: 10px;
            font-size: 24px;
            cursor: pointer;
        }
        .wifi-network {
            padding: 10px;
            border: 1px solid #ddd;
            margin: 5px 0;
            border-radius: 4px;
            cursor: pointer;
        }
        .wifi-network:hover {
            background-color: #f8f9fa;
        }
        .wifi-status-connected {
            color: #28a745;
            font-weight: bold;
        }
        .wifi-status-disconnected {
            color: #dc3545;
            font-weight: bold;
        }
        .status-info {
            background: #f8f9fa;
            padding: 10px;
            border-radius: 4px;
            margin: 10px 0;
        }
        .status-info p {
            margin: 5px 0;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP8266 WiFi Gateway</h1>
        
        <div class="tabs">
            <button class="tab-button active" onclick="openTab('ap-settings')">AP Settings</button>
            <button class="tab-button" onclick="openTab('wifi-scan')">WiFi Scan</button>
            <button class="tab-button" onclick="openTab('device-settings')">Device Settings</button>
            <button class="tab-button" onclick="openTab('device-management')">Device Management</button>
            <button class="tab-button" onclick="openTab('status')">Status</button>
        </div>
        
        <div id="ap-settings" class="tab-content active">
            <div class="section">
                <h3 class="black-header">AP Settings</h3>
                <form id="apSettingsForm">
                    <label for="ap_ssid">AP SSID:</label>
                    <input type="text" id="ap_ssid" name="ap_ssid" required>
                    
                    <label for="ap_password">AP Password:</label>
                    <input type="password" id="ap_password" name="ap_password" required minlength="8">
                    
                    <label for="subnet">Subnet (192.168.XXX.1):</label>
                    <select id="subnet" name="subnet"></select>
                    
                    <label>
                        <input type="checkbox" id="ap_enabled" name="ap_enabled"> AP Enabled
                    </label>
                    
                    <button type="submit">Save AP Settings</button>
                </form>
            </div>
        </div>
        
        <div id="wifi-scan" class="tab-content">
            <div class="section">
                <h3 class="black-header">WiFi Scan</h3>
                
                <div class="section">
                    <h4>WiFi Connection Status</h4>
                    <div id="wifiStatus">
                        <p>Loading WiFi status...</p>
                    </div>
                    <button onclick="loadWiFiStatus()">Refresh Status</button>
                    <button id="disconnectBtn" class="disconnect-btn" onclick="disconnectWiFi()" style="display: none;">Disconnect WiFi</button>
                </div>
                
                <div class="section">
                    <h4>Available WiFi Networks</h4>
                    <button onclick="scanWiFi()">Scan Networks</button>
                    <div id="wifiNetworks"></div>
                </div>
                
                <div class="section">
                    <h4>Connect to WiFi</h4>
                    <form id="wifiConnectForm">
                        <label for="wifi_ssid">SSID:</label>
                        <input type="text" id="wifi_ssid" name="wifi_ssid" required>
                        
                        <label for="wifi_password">Password:</label>
                        <input type="password" id="wifi_password" name="wifi_password">
                        
                        <label>
                            <input type="checkbox" id="sta_enabled" name="sta_enabled"> WiFi Client Enabled
                        </label>
                        
                        <button type="submit">Connect</button>
                    </form>
                </div>
            </div>
        </div>
        
    <div id="device-settings" class="tab-content">
      <div class="section">
        <h3 class="black-header">Device Settings</h3>
        <form id="deviceSettingsForm">
          <label for="device_name">Device Name:</label>
          <input type="text" id="device_name" name="device_name" required>
          
          <label for="device_comment">Device Comment:</label>
          <textarea id="device_comment" name="device_comment" rows="3"></textarea>
          
          <div class="section">
            <div class="section-title">System Attributes</div>
            <p style="font-size: 12px; color: #666; margin-bottom: 10px;">
              Add system attributes as key-value pairs
            </p>
            <div class="attributes-container" id="attributesContainer"></div>
            <button type="button" onclick="addAttribute()" style="background: #17a2b8;">Add Attribute</button>
          </div>
          
          <button type="submit">Save Device Settings</button>
          <button type="button" onclick="refreshDeviceSettings()" style="background: #28a745;">Refresh Device Settings</button>
        </form>
      </div>
    </div>
        
        <div id="device-management" class="tab-content">
            <div class="section">
                <h3 class="black-header">Device Management</h3>
                <button onclick="clearSettings()" style="background: #dc3545;">Clear All Settings</button>
                <button onclick="restartDevice()" style="background: #ffc107; color: black;">Restart Device</button>
            </div>
        </div>
        
        <div id="status" class="tab-content">
            <div class="section">
                <h3 class="black-header">Status</h3>
                <h4>Connected Devices</h4>
                <button onclick="window.loadConnectedDevices2()">Refresh</button>
                <table id="connectedDevicesTable">
                    <thead>
                        <tr>
                            <th>IP Address</th>
                            <th>MAC Address</th>
                            <th>Device Name</th>
                            <th>Comment</th>
                            <th>Actions</th>
                        </tr>
                    </thead>
                    <tbody></tbody>
                </table>
            </div>
        </div>
    </div>

    <div id="deviceModal" class="modal">
        <div class="modal-content">
            <span class="close">&times;</span>
            <h3>Device Information</h3>
            <form id="deviceInfoForm">
                <input type="hidden" id="modal_ip" name="ip">
                <input type="hidden" id="modal_mac" name="mac">
                
                <label for="modal_name">Device Name:</label>
                <input type="text" id="modal_name" name="name" required>
                
                <label for="modal_comment">Comment:</label>
                <textarea id="modal_comment" name="comment" rows="3"></textarea>
                
                <button type="submit">Save</button>
            </form>
        </div>
    </div>
)rawliteral");

  client.stop();
}

void handleGetSettings() {
  DynamicJsonDocument doc(1024);
  doc["ap_ssid"] = settings.ap_ssid;
  doc["ap_password"] = settings.ap_password;
  doc["sta_ssid"] = settings.sta_ssid;
  doc["sta_password"] = settings.sta_password;
  doc["device_name"] = settings.device_name;
  doc["device_comment"] = settings.device_comment;
  doc["json_attributes"] = settings.json_attributes;
  doc["subnet"] = settings.subnet;
  doc["ap_enabled"] = settings.ap_enabled;
  doc["sta_enabled"] = settings.sta_enabled;
  
  String response;
  serializeJson(doc, response);
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", response);
}

void handlePostSettings() {
  String body = server.arg("plain");
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, body);
  
  bool wifiSettingsChanged = false;
  bool apSettingsChanged = false;
  
  // Проверяем, какие настройки изменились
  if (doc.containsKey("ap_ssid") && strcmp(settings.ap_ssid, doc["ap_ssid"]) != 0) {
    strlcpy(settings.ap_ssid, doc["ap_ssid"], sizeof(settings.ap_ssid));
    apSettingsChanged = true;
  }
  
  if (doc.containsKey("ap_password") && strcmp(settings.ap_password, doc["ap_password"]) != 0) {
    strlcpy(settings.ap_password, doc["ap_password"], sizeof(settings.ap_password));
    apSettingsChanged = true;
  }
  
  if (doc.containsKey("subnet") && settings.subnet != doc["subnet"]) {
    settings.subnet = doc["subnet"];
    apSettingsChanged = true;
  }
  
  if (doc.containsKey("ap_enabled") && settings.ap_enabled != doc["ap_enabled"]) {
    settings.ap_enabled = doc["ap_enabled"];
    apSettingsChanged = true;
  }
  
  if (doc.containsKey("sta_enabled") && settings.sta_enabled != doc["sta_enabled"]) {
    settings.sta_enabled = doc["sta_enabled"];
    wifiSettingsChanged = true;
  }
  
  if (doc.containsKey("ssid") && strcmp(settings.sta_ssid, doc["ssid"]) != 0) {
    strlcpy(settings.sta_ssid, doc["ssid"], sizeof(settings.sta_ssid));
    wifiSettingsChanged = true;
  }
  
  if (doc.containsKey("password") && strcmp(settings.sta_password, doc["password"]) != 0) {
    strlcpy(settings.sta_password, doc["password"], sizeof(settings.sta_password));
    wifiSettingsChanged = true;
  }
  
  // Настройки устройства (не требуют перезапуска WiFi)
  if (doc.containsKey("device_name")) {
    strlcpy(settings.device_name, doc["device_name"], sizeof(settings.device_name));
  }
  
  if (doc.containsKey("device_comment")) {
    strlcpy(settings.device_comment, doc["device_comment"], sizeof(settings.device_comment));
  }
  
  if (doc.containsKey("json_attributes")) {
    strlcpy(settings.json_attributes, doc["json_attributes"], sizeof(settings.json_attributes));
  }
  
  // Сохраняем настройки
  saveSettings();
  
  // Перезапускаем WiFi только если изменились соответствующие настройки
  if (wifiSettingsChanged || apSettingsChanged) {
    needsWiFiRestart = true;
  }
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"success\"}");
}

void handleWiFiScan() {
  WiFiClient client = server.client();
  
  // Send HTTP headers
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();
  int n = WiFi.scanNetworks();
  String jsonString = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) {
      jsonString += ",";
    }
    jsonString += "{";
    jsonString += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    jsonString += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    jsonString += "\"encryption\":\"" + String((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "open" : "secured") + "\"";
    jsonString += "}";
  }
  jsonString += "]";
  wifiNetworks = jsonString;
  WiFi.scanDelete();
  client.println(wifiNetworks);
  client.stop();
}

void handleGetWiFiStatus() {
  DynamicJsonDocument doc(512);
  
  if (WiFi.status() == WL_CONNECTED) {
    doc["status"] = "connected";
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["gateway"] = WiFi.gatewayIP().toString();
    doc["subnet"] = WiFi.subnetMask().toString();
    doc["mac"] = WiFi.macAddress();
    doc["rssi"] = WiFi.RSSI();
  } else {
    doc["status"] = "disconnected";
  }
  
  String response;
  serializeJson(doc, response);
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", response);
}

void handleWiFiConnect() {
  String body = server.arg("plain");
  DynamicJsonDocument doc(256);
  deserializeJson(doc, body);
  
  if (doc.containsKey("ssid") && doc.containsKey("password")) {
    strlcpy(settings.sta_ssid, doc["ssid"], sizeof(settings.sta_ssid));
    strlcpy(settings.sta_password, doc["password"], sizeof(settings.sta_password));
    settings.sta_enabled = doc.containsKey("sta_enabled") ? doc["sta_enabled"] : true;
    
    saveSettings();
    needsWiFiRestart = true;
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"status\":\"connecting\"}");
  } else {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing ssid or password\"}");
  }
}

void handleWiFiDisconnect() {
  // Отключаем STA режим
  settings.sta_enabled = false;
  strcpy(settings.sta_ssid, "");
  strcpy(settings.sta_password, "");
  
  saveSettings();
  needsWiFiRestart = true;
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"disconnected\"}");
}

void handleGetConnectedDevices() {
  String json = getConnectedDevicesJSON();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleUpdateDevice() {
  String body = server.arg("plain");
  Serial.println("Received device update: " + body);
  
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    Serial.println("JSON parse error: " + String(error.c_str()));
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parse error\"}");
    return;
  }
  
  // Сохраняем информацию об устройстве по MAC адресу
  if (doc.containsKey("mac") && doc.containsKey("name") && doc.containsKey("comment")) {
    String mac = doc["mac"].as<String>();
    String name = doc["name"].as<String>();
    String comment = doc["comment"].as<String>();
    
    updateDeviceInfo(mac, name, comment);
    Serial.println("Updated device info for MAC: " + mac + ", Name: " + name + ", Comment: " + comment);
  }
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"success\"}");
}

void handleClearSettings() {
  memset(&settings, 0, sizeof(settings));
  strcpy(settings.ap_ssid, "VR_APP_ESP");
  strcpy(settings.ap_password, "12345678");
  strcpy(settings.device_name, "ESP8266_Device");
  strcpy(settings.json_attributes, "{}");
  settings.subnet = 4;
  settings.ap_enabled = true;
  settings.sta_enabled = false;
  settings.settings_valid = true;
  
  // Очищаем информацию об устройствах
  clearDeviceInfo();
  
  saveSettings();
  needsWiFiRestart = true;
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"success\"}");
}

void handleDeviceRegister() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  
  if (server.method() == HTTP_OPTIONS) {
    server.send(200);
    return;
  }
  
  String body = server.arg("plain");
  Serial.println("Device registration: " + body);
  
  server.send(200, "application/json", "{\"status\":\"registered\"}");
}

void handleDeviceUpdate() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  
  if (server.method() == HTTP_OPTIONS) {
    server.send(200);
    return;
  }
  
  String body = server.arg("plain");
  Serial.println("Device update: " + body);
  
  DynamicJsonDocument doc(512);
  deserializeJson(doc, body);
  
  if (doc.containsKey("device_name")) strlcpy(settings.device_name, doc["device_name"], sizeof(settings.device_name));
  if (doc.containsKey("device_comment")) strlcpy(settings.device_comment, doc["device_comment"], sizeof(settings.device_comment));
  if (doc.containsKey("attributes")) strlcpy(settings.json_attributes, doc["attributes"], sizeof(settings.json_attributes));
  
  saveSettings();
  
  server.send(200, "application/json", "{\"status\":\"updated\"}");
}

void handleGetConnectedDevicesAPI() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", getConnectedDevicesJSON());
}

// Новый API endpoint для получения настроек устройства
void handleGetDeviceSettingsAPI() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  
  if (server.method() == HTTP_OPTIONS) {
    server.send(200);
    return;
  }
  
  // Получаем параметр MAC из запроса
  String mac = server.arg("mac");
  
  DynamicJsonDocument doc(1024);
  
  if (mac.length() > 0) {
    // Загружаем информацию об устройстве по MAC адресу
    DeviceInfo deviceInfo = loadDeviceInfo(mac);
    
    if (deviceInfo.valid) {
      doc["device_name"] = deviceInfo.name;
      doc["device_comment"] = deviceInfo.comment;
    } else {
      // Если информации об устройстве нет, используем общие настройки
      doc["device_name"] = settings.device_name;
      doc["device_comment"] = settings.device_comment;
    }
  } else {
    // Если MAC не указан, возвращаем общие настройки
    doc["device_name"] = settings.device_name;
    doc["device_comment"] = settings.device_comment;
  }
  
  // System Attributes всегда возвращаем из общих настроек
  DynamicJsonDocument attrDoc(512);
  deserializeJson(attrDoc, settings.json_attributes);
  doc["system_attributes"] = attrDoc.as<JsonObject>();
  
  String response;
  serializeJson(doc, response);
  
  server.send(200, "application/json", response);
}

void scanWiFiNetworks() {
  int n = WiFi.scanNetworks();
  DynamicJsonDocument doc(4096);
  JsonArray networks = doc.to<JsonArray>();
  
  for (int i = 0; i < n; i++) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["encryption"] = (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "open" : "secured";
  }
  
  wifiNetworks = "";
  serializeJson(doc, wifiNetworks);
  
  WiFi.scanDelete();
}

String getConnectedDevicesJSON() {
  DynamicJsonDocument doc(2048);
  JsonArray devices = doc.to<JsonArray>();
  
  struct station_info *station = wifi_softap_get_station_info();
  while (station != NULL) {
    JsonObject device = devices.createNestedObject();
    String mac = mac2str(station->bssid);
    
    device["ip"] = IPAddress(station->ip.addr).toString();
    device["mac"] = mac;
    
    // Загружаем сохраненную информацию об устройстве по MAC адресу
    DeviceInfo deviceInfo = loadDeviceInfo(mac);
    if (deviceInfo.valid) {
      device["name"] = deviceInfo.name;
      device["comment"] = deviceInfo.comment;
    } else {
      // Если информации нет, используем общие настройки
      device["name"] = settings.device_name;
      device["comment"] = settings.device_comment;
    }
    
    device["attributes"] = settings.json_attributes;
    
    station = STAILQ_NEXT(station, next);
  }
  wifi_softap_free_station_info();
  
  String json;
  serializeJson(doc, json);
  return json;
}

String mac2str(const uint8_t* mac) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

void loadSettings() {
  EEPROM.get(SETTINGS_ADDR, settings);
  if (!settings.settings_valid) {
    memset(&settings, 0, sizeof(settings));
    strcpy(settings.ap_ssid, "VR_APP_ESP");
    strcpy(settings.ap_password, "12345678");
    strcpy(settings.device_name, "ESP8266_Device");
    strcpy(settings.json_attributes, "{}");
    settings.subnet = 4;
    settings.ap_enabled = true;
    settings.sta_enabled = false;
    settings.settings_valid = true;
    saveSettings();
  }
  Serial.println("Settings loaded from EEPROM");
  Serial.println("Device name: " + String(settings.device_name));
  Serial.println("Device comment: " + String(settings.device_comment));
  Serial.println("JSON attributes: " + String(settings.json_attributes));
}

void saveSettings() {
  EEPROM.put(SETTINGS_ADDR, settings);
  if (EEPROM.commit()) {
    Serial.println("Settings saved to EEPROM successfully");
    Serial.println("Device name: " + String(settings.device_name));
    Serial.println("Device comment: " + String(settings.device_comment));
    Serial.println("JSON attributes: " + String(settings.json_attributes));
  } else {
    Serial.println("Error saving settings to EEPROM");
  }
}

// Функции для работы с информацией об устройствах
void saveDeviceInfo(const String& mac, const String& name, const String& comment) {
  DeviceInfo deviceInfo;
  int addr = DEVICE_INFO_ADDR;
  
  // Ищем существующую запись или свободное место
  for (int i = 0; i < MAX_DEVICES; i++) {
    EEPROM.get(addr, deviceInfo);
    
    if (!deviceInfo.valid || strcmp(deviceInfo.mac, mac.c_str()) == 0) {
      // Нашли свободное место или существующую запись
      strlcpy(deviceInfo.mac, mac.c_str(), sizeof(deviceInfo.mac));
      strlcpy(deviceInfo.name, name.c_str(), sizeof(deviceInfo.name));
      strlcpy(deviceInfo.comment, comment.c_str(), sizeof(deviceInfo.comment));
      deviceInfo.valid = true;
      
      EEPROM.put(addr, deviceInfo);
      EEPROM.commit();
      
      Serial.println("Saved device info for MAC: " + mac);
      return;
    }
    
    addr += sizeof(DeviceInfo);
  }
  
  Serial.println("No free space to save device info for MAC: " + mac);
}

DeviceInfo loadDeviceInfo(const String& mac) {
  DeviceInfo deviceInfo;
  int addr = DEVICE_INFO_ADDR;
  
  for (int i = 0; i < MAX_DEVICES; i++) {
    EEPROM.get(addr, deviceInfo);
    
    if (deviceInfo.valid && strcmp(deviceInfo.mac, mac.c_str()) == 0) {
      Serial.println("Loaded device info for MAC: " + mac);
      return deviceInfo;
    }
    
    addr += sizeof(DeviceInfo);
  }
  
  // Возвращаем пустую структуру, если устройство не найдено
  DeviceInfo empty;
  empty.valid = false;
  return empty;
}

void updateDeviceInfo(const String& mac, const String& name, const String& comment) {
  saveDeviceInfo(mac, name, comment);
}

void clearDeviceInfo() {
  DeviceInfo deviceInfo;
  int addr = DEVICE_INFO_ADDR;
  
  for (int i = 0; i < MAX_DEVICES; i++) {
    memset(&deviceInfo, 0, sizeof(DeviceInfo));
    EEPROM.put(addr, deviceInfo);
    addr += sizeof(DeviceInfo);
  }
  
  EEPROM.commit();
  Serial.println("Cleared all device info");
}
