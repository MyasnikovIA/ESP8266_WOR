#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

#define EEPROM_SIZE 4096
#define MAX_DEVICES 20
#define BOOT_BUTTON_PIN 0  // GPIO0 для кнопки Boot
#define SERIAL_COMMAND_BUFFER_SIZE 128

WebServer server(80);

// Структура для хранения настроек устройства
struct DeviceConfig {
    char mac[18];
    bool isDevice;
    char comment[64];
    uint32_t ip; // Сохраняем IP как 32-битное число
    bool hasStaticIP;
};

// Структура для настроек AP
struct APConfig {
    int subnet; // Например: 192.168.X.0
    char ssid[32];
    char password[64];
};

APConfig apConfig;
DeviceConfig devices[MAX_DEVICES];
int deviceCount = 0;

// Состояние сканирования
bool scanInProgress = false;
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 30000; // 30 секунд

// Переменные для обработки кнопки Boot
unsigned long lastButtonPress = 0;
const unsigned long BUTTON_PRESS_DURATION = 5000; // 5 секунд для очистки EEPROM
bool buttonPressed = false;
unsigned long buttonPressStartTime = 0;

// Буфер для команд Serial
char serialCommandBuffer[SERIAL_COMMAND_BUFFER_SIZE];
int serialCommandIndex = 0;

// Вспомогательная функция для преобразования IP
IPAddress uint32ToIP(uint32_t ip) {
    return IPAddress(ip);
}

uint32_t ipToUint32(const IPAddress& ip) {
    return (uint32_t)ip;
}

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("ESP32 AP Configuration System");
    Serial.println("==============================");
    delay(1000);
    //clearEEPROM();
    // Настройка пина кнопки Boot
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    
    // Инициализация EEPROM
    EEPROM.begin(EEPROM_SIZE);
    
    // Загрузка конфигурации
    loadConfig();
    
    // Настройка AP
    setupAP();
    
    // Настройка REST API
    setupAPI();
    
    Serial.println("\nAP started!");
    Serial.print("SSID: ");
    Serial.println(apConfig.ssid);
    Serial.print("IP адрес: ");
    Serial.println(WiFi.softAPIP());
    
    // Вывод инструкций по Serial
    printSerialInstructions();
    
    // Запуск сервера
    server.begin();
}

void loop() {
    server.handleClient();
    
    // Обработка нажатия кнопки Boot
    handleBootButton();
    
    // Обработка команд из Serial
    handleSerialCommands();
    
    // Периодическое сканирование сети
    if (millis() - lastScanTime > SCAN_INTERVAL && !scanInProgress) {
        scanNetwork();
    }
}

void printSerialInstructions() {
    Serial.println("\n=== Serial Commands ===");
    Serial.println("help - Show this help");
    Serial.println("clear - Clear EEPROM");
    Serial.println("config - Show current configuration");
    Serial.println("scan - Start network scan");
    Serial.println("save - Save configuration to EEPROM");
    Serial.println("load - Load configuration from EEPROM");
    Serial.println("setsubnet X - Set subnet (0-255)");
    Serial.println("setssid NAME - Set SSID");
    Serial.println("setpassword PASS - Set password");
    Serial.println("devices - List all devices");
    Serial.println("adddevice MAC [COMMENT] - Add device (MAC format: XX:XX:XX:XX:XX:XX)");
    Serial.println("removedevice MAC - Remove device");
    Serial.println("setdevice MAC isDevice [COMMENT] - Set device properties");
    Serial.println("========================\n");
}

void handleBootButton() {
    // Проверяем состояние кнопки Boot (активный низкий уровень)
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (!buttonPressed) {
            // Запоминаем время начала нажатия
            buttonPressed = true;
            buttonPressStartTime = millis();
            Serial.println("Boot button pressed. Hold for 5 seconds to clear EEPROM...");
        }
        
        // Если кнопка удерживается дольше заданного времени
        if (millis() - buttonPressStartTime > BUTTON_PRESS_DURATION) {
            clearEEPROM();
            buttonPressed = false;
        }
    } else {
        if (buttonPressed) {
            // Кнопка отпущена до истечения времени
            unsigned long pressDuration = millis() - buttonPressStartTime;
            Serial.print("Boot button released after ");
            Serial.print(pressDuration / 1000.0);
            Serial.println(" seconds");
            buttonPressed = false;
        }
    }
}

void handleSerialCommands() {
    while (Serial.available() > 0) {
        char c = Serial.read();
        
        // Обработка символа новой строки
        if (c == '\n' || c == '\r') {
            if (serialCommandIndex > 0) {
                serialCommandBuffer[serialCommandIndex] = '\0';
                processSerialCommand(serialCommandBuffer);
                serialCommandIndex = 0;
            }
        } 
        // Добавление символа в буфер
        else if (serialCommandIndex < SERIAL_COMMAND_BUFFER_SIZE - 1) {
            serialCommandBuffer[serialCommandIndex++] = c;
        }
    }
}

void processSerialCommand(const char* command) {
    Serial.print("> ");
    Serial.println(command);
    
    // Создаем копию команды для токенизации
    char cmdCopy[SERIAL_COMMAND_BUFFER_SIZE];
    strcpy(cmdCopy, command);
    
    // Разделяем команду на токены
    char* token = strtok(cmdCopy, " ");
    
    if (token == NULL) return;
    
    // Обработка команд
    if (strcmp(token, "help") == 0) {
        printSerialInstructions();
    }
    else if (strcmp(token, "clear") == 0) {
        clearEEPROM();
    }
    else if (strcmp(token, "config") == 0) {
        printConfig();
    }
    else if (strcmp(token, "scan") == 0) {
        scanNetwork();
    }
    else if (strcmp(token, "save") == 0) {
        saveConfig();
        Serial.println("Configuration saved to EEPROM");
    }
    else if (strcmp(token, "load") == 0) {
        loadConfig();
        Serial.println("Configuration loaded from EEPROM");
    }
    else if (strcmp(token, "setsubnet") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            int subnet = atoi(token);
            if (subnet >= 0 && subnet <= 255) {
                apConfig.subnet = subnet;
                Serial.print("Subnet set to 192.168.");
                Serial.print(subnet);
                Serial.println(".0");
                saveConfig();
                WiFi.softAPdisconnect(true);
                delay(100);
                setupAP();
            } else {
                Serial.println("Invalid subnet value (0-255)");
            }
        }
    }
    else if (strcmp(token, "setssid") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            strcpy(apConfig.ssid, token);
            Serial.print("SSID set to: ");
            Serial.println(apConfig.ssid);
            saveConfig();
            WiFi.softAPdisconnect(true);
            delay(100);
            setupAP();
        }
    }
    else if (strcmp(token, "setpassword") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            strcpy(apConfig.password, token);
            Serial.println("Password updated");
            saveConfig();
            WiFi.softAPdisconnect(true);
            delay(100);
            setupAP();
        }
    }
    else if (strcmp(token, "devices") == 0) {
        printDevices();
    }
    else if (strcmp(token, "adddevice") == 0) {
        token = strtok(NULL, " ");
        if (token && deviceCount < MAX_DEVICES) {
            // Проверяем формат MAC адреса
            if (isValidMAC(token)) {
                int index = findDeviceByMAC(token);
                if (index == -1) {
                    index = deviceCount++;
                    strcpy(devices[index].mac, token);
                    devices[index].isDevice = false;
                    devices[index].comment[0] = '\0';
                    devices[index].hasStaticIP = false;
                    // Используем IP адрес по умолчанию
                    devices[index].ip = ipToUint32(IPAddress(192, 168, apConfig.subnet, 100 + index));
                    
                    // Проверяем наличие комментария
                    token = strtok(NULL, "");
                    if (token) {
                        strncpy(devices[index].comment, token, 63);
                        devices[index].comment[63] = '\0';
                    }
                    
                    Serial.print("Device added: ");
                    Serial.println(token);
                    saveConfig();
                } else {
                    Serial.println("Device already exists");
                }
            } else {
                Serial.println("Invalid MAC address format (use XX:XX:XX:XX:XX:XX)");
            }
        }
    }
    else if (strcmp(token, "removedevice") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            int index = findDeviceByMAC(token);
            if (index != -1) {
                // Сдвигаем все последующие устройства
                for (int i = index; i < deviceCount - 1; i++) {
                    devices[i] = devices[i + 1];
                }
                deviceCount--;
                Serial.print("Device removed: ");
                Serial.println(token);
                saveConfig();
            } else {
                Serial.println("Device not found");
            }
        }
    }
    else if (strcmp(token, "setdevice") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            int index = findDeviceByMAC(token);
            if (index != -1) {
                token = strtok(NULL, " ");
                if (token) {
                    devices[index].isDevice = (strcmp(token, "true") == 0 || strcmp(token, "1") == 0);
                    
                    // Проверяем наличие комментария
                    token = strtok(NULL, "");
                    if (token) {
                        strncpy(devices[index].comment, token, 63);
                        devices[index].comment[63] = '\0';
                    }
                    
                    Serial.print("Device updated: ");
                    Serial.println(devices[index].mac);
                    saveConfig();
                }
            } else {
                Serial.println("Device not found");
            }
        }
    }
    else {
        Serial.println("Unknown command. Type 'help' for available commands.");
    }
}

void clearEEPROM() {
    Serial.println("\n=== CLEARING EEPROM ===");
    
    // Заполняем EEPROM нулями
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
    
    // Сбрасываем конфигурацию в памяти
    apConfig.subnet = 4;
    strcpy(apConfig.ssid, "ESP32_AP");
    strcpy(apConfig.password, "12345678");
    deviceCount = 0;
    
    // Перезапускаем AP с настройками по умолчанию
    WiFi.softAPdisconnect(true);
    delay(100);
    setupAP();
    
    Serial.println("EEPROM cleared and default configuration restored");
    Serial.println("AP restarted with default settings");
    Serial.print("SSID: ");
    Serial.println(apConfig.ssid);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
}

void printConfig() {
    Serial.println("\n=== CURRENT CONFIGURATION ===");
    Serial.print("SSID: ");
    Serial.println(apConfig.ssid);
    Serial.print("Subnet: 192.168.");
    Serial.print(apConfig.subnet);
    Serial.println(".0");
    Serial.print("Device count: ");
    Serial.println(deviceCount);
    Serial.println("=============================\n");
}

void printDevices() {
    Serial.println("\n=== CONNECTED DEVICES ===");
    Serial.println("MAC Address\t\tIP Address\t\tisDevice\tComment");
    Serial.println("-------------------------------------------------------------------------");
    
    for (int i = 0; i < deviceCount; i++) {
        Serial.print(devices[i].mac);
        Serial.print("\t");
        
        // Безопасный вывод IP адреса
        IPAddress ip = uint32ToIP(devices[i].ip);
        Serial.print(ip);
        
        Serial.print("\t");
        Serial.print(devices[i].isDevice ? "Yes" : "No");
        Serial.print("\t\t");
        Serial.println(devices[i].comment);
    }
    
    if (deviceCount == 0) {
        Serial.println("No devices found");
    }
    
    Serial.println("==================================\n");
}

bool isValidMAC(const char* mac) {
    // Проверяем базовый формат MAC адреса (XX:XX:XX:XX:XX:XX)
    int len = strlen(mac);
    if (len != 17) return false;
    
    for (int i = 0; i < len; i++) {
        if ((i + 1) % 3 == 0) {
            if (mac[i] != ':') return false;
        } else {
            if (!isxdigit(mac[i])) return false;
        }
    }
    return true;
}

void setupAP() {
    // Настройка подсети
    char subnetMask[16];
    sprintf(subnetMask, "255.255.255.0");
    
    IPAddress localIP(192, 168, apConfig.subnet, 1);
    IPAddress gateway(192, 168, apConfig.subnet, 1);
    IPAddress subnet(255, 255, 255, 0);
    
    WiFi.softAPConfig(localIP, gateway, subnet);
    WiFi.softAP(apConfig.ssid, apConfig.password);
    
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
}

void setupAPI() {
    // Кросс-доменные заголовки для всех запросов
    server.enableCORS(true);
    
    // Главная страница
    server.on("/", HTTP_GET, []() {
        sendHTML();
    });

    // Получение списка всех устройств
    server.on("/api/devices", HTTP_GET, []() {
        DynamicJsonDocument doc(4096);
        JsonArray devicesArray = doc.to<JsonArray>();
        
        for (int i = 0; i < deviceCount; i++) {
            JsonObject device = devicesArray.createNestedObject();
            device["mac"] = devices[i].mac;
            device["isDevice"] = devices[i].isDevice;
            device["comment"] = devices[i].comment;
            
            // Безопасное преобразование IP
            IPAddress ip = uint32ToIP(devices[i].ip);
            device["ip"] = ip.toString();
            
            device["hasStaticIP"] = devices[i].hasStaticIP;
        }
        
        String response;
        serializeJson(doc, response);
        
        server.send(200, "application/json", response);
    });

    // Получение списка устройств с isDevice = true
    server.on("/api/devices/isdevice", HTTP_GET, []() {
        DynamicJsonDocument doc(2048);
        JsonArray devicesArray = doc.to<JsonArray>();
        
        for (int i = 0; i < deviceCount; i++) {
            if (devices[i].isDevice) {
                JsonObject device = devicesArray.createNestedObject();
                device["mac"] = devices[i].mac;
                device["comment"] = devices[i].comment;
                
                // Безопасное преобразование IP
                IPAddress ip = uint32ToIP(devices[i].ip);
                device["ip"] = ip.toString();
            }
        }
        
        String response;
        serializeJson(doc, response);
        
        server.send(200, "application/json", response);
    });

    // Обновление настроек устройства
    server.on("/api/device", HTTP_POST, []() {
        if (server.hasArg("plain")) {
            DynamicJsonDocument doc(512);
            deserializeJson(doc, server.arg("plain"));
            
            const char* mac = doc["mac"];
            bool isDevice = doc["isDevice"];
            const char* comment = doc["comment"];
            bool hasStaticIP = doc["hasStaticIP"];
            const char* ipStr = doc["ip"];
            
            // Поиск устройства по MAC
            int index = findDeviceByMAC(mac);
            if (index == -1) {
                // Новое устройство
                if (deviceCount < MAX_DEVICES) {
                    index = deviceCount++;
                    strcpy(devices[index].mac, mac);
                }
            }
            
            if (index != -1) {
                devices[index].isDevice = isDevice;
                strcpy(devices[index].comment, comment);
                devices[index].hasStaticIP = hasStaticIP;
                if (hasStaticIP && ipStr) {
                    IPAddress ip;
                    if (ip.fromString(ipStr)) {
                        devices[index].ip = ipToUint32(ip);
                    }
                }
                
                saveConfig();
                
                server.send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                server.send(400, "application/json", "{\"error\":\"Too many devices\"}");
            }
        } else {
            server.send(400, "application/json", "{\"error\":\"No data\"}");
        }
    });

    // Обновление настроек AP
    server.on("/api/ap", HTTP_POST, []() {
        if (server.hasArg("plain")) {
            DynamicJsonDocument doc(256);
            deserializeJson(doc, server.arg("plain"));
            
            apConfig.subnet = doc["subnet"];
            const char* ssid = doc["ssid"];
            const char* password = doc["password"];
            
            if (ssid) strcpy(apConfig.ssid, ssid);
            if (password) strcpy(apConfig.password, password);
            
            saveConfig();
            
            // Перезапуск AP с новыми настройками
            WiFi.softAPdisconnect(true);
            delay(100);
            setupAP();
            
            server.send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            server.send(400, "application/json", "{\"error\":\"No data\"}");
        }
    });

    // Получение настроек AP
    server.on("/api/ap", HTTP_GET, []() {
        DynamicJsonDocument doc(256);
        doc["subnet"] = apConfig.subnet;
        doc["ssid"] = apConfig.ssid;
        // Пароль не отправляем в целях безопасности
        
        String response;
        serializeJson(doc, response);
        
        server.send(200, "application/json", response);
    });

    // Сканирование сети
    server.on("/api/scan", HTTP_POST, []() {
        scanNetwork();
        server.send(200, "application/json", "{\"status\":\"scanning\"}");
    });
}

void sendHTML() {
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
    client.println("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
    client.println("<title>ESP32 AP Configuration</title>");
    client.println("<style>");
    client.println("body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }");
    client.println(".container { max-width: 1200px; margin: 0 auto; }");
    client.println(".section { margin-bottom: 30px; padding: 20px; background-color: white; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }");
    client.println("table { width: 100%; border-collapse: collapse; margin-top: 10px; }");
    client.println("th, td { border: 1px solid #ddd; padding: 10px; text-align: left; }");
    client.println("th { background-color: #4CAF50; color: white; }");
    client.println("tr:nth-child(even) { background-color: #f9f9f9; }");
    client.println("tr:hover { background-color: #f1f1f1; }");
    client.println("button { padding: 8px 16px; margin: 5px; cursor: pointer; background-color: #4CAF50; color: white; border: none; border-radius: 4px; }");
    client.println("button:hover { background-color: #45a049; }");
    client.println("input[type='text'], input[type='password'], input[type='number'] { padding: 8px; margin: 5px 0; width: 200px; border: 1px solid #ddd; border-radius: 4px; }");
    client.println(".ip-link { color: #0066cc; text-decoration: none; }");
    client.println(".ip-link:hover { text-decoration: underline; }");
    client.println("</style>");
    client.println("</head>");
    client.println("<body>");
    client.println("<div class='container'>");
    client.println("<h1>ESP32 Access Point Configuration</h1>");
    
    // Секция настроек AP
    client.println("<div class='section'>");
    client.println("<h2>AP Settings</h2>");
    client.println("<div>");
    client.println("<label>Subnet (192.168.X.0): </label>");
    client.println("<input type='number' id='subnet' min='0' max='255' value='");
    client.print(apConfig.subnet);
    client.println("'>");
    client.println("</div>");
    client.println("<div style='margin-top: 10px;'>");
    client.println("<label>SSID: </label>");
    client.println("<input type='text' id='ssid' value='");
    client.print(apConfig.ssid);
    client.println("'>");
    client.println("</div>");
    client.println("<div style='margin-top: 10px;'>");
    client.println("<label>Password: </label>");
    client.println("<input type='password' id='password' placeholder='New password'>");
    client.println("</div>");
    client.println("<button onclick='saveAPSettings()'>Save AP Settings</button>");
    client.println("</div>");
    
    // Секция устройств
    client.println("<div class='section'>");
    client.println("<h2>Connected Devices</h2>");
    client.println("<button onclick='loadDevices()'>Refresh</button>");
    client.println("<button onclick='startScan()'>Scan Network</button>");
    client.println("<div id='devices'></div>");
    client.println("</div>");
    
    // JavaScript
    client.println("<script>");
    client.println("function loadDevices() {");
    client.println("  fetch('/api/devices')");
    client.println("    .then(response => response.json())");
    client.println("    .then(data => displayDevices(data));");
    client.println("}");
    
    client.println("function displayDevices(devices) {");
    client.println("  const container = document.getElementById('devices');");
    client.println("  let html = '<table><tr><th>MAC</th><th>IP</th><th>isDevice</th><th>Comment</th><th>Static IP</th><th>Actions</th></tr>';");
    client.println("  devices.forEach(device => {");
    client.println("    html += '<tr>';");
    client.println("    html += '<td>' + device.mac + '</td>';");
    client.println("    html += '<td><a class=\"ip-link\" href=\"http://' + device.ip + '\" target=\"_blank\">' + device.ip + '</a></td>';");
    client.println("    html += '<td><input type=\"checkbox\" ' + (device.isDevice ? 'checked' : '') + ' onchange=\"updateDevice(\\'' + device.mac + '\\', this.checked, \\'' + device.comment + '\\', ' + device.hasStaticIP + ', \\'' + device.ip + '\\')\"></td>';");
    client.println("    html += '<td><input type=\"text\" value=\"' + device.comment + '\" onchange=\"updateDevice(\\'' + device.mac + '\\', ' + device.isDevice + ', this.value, ' + device.hasStaticIP + ', \\'' + device.ip + '\\')\"></td>';");
    client.println("    html += '<td>' + (device.hasStaticIP ? 'Yes' : 'No') + '</td>';");
    client.println("    html += '<td>';");
    client.println("    html += '<button onclick=\"setStaticIP(\\'' + device.mac + '\\', \\'' + device.ip + '\\')\">Set Static IP</button>';");
    client.println("    html += '</td>';");
    client.println("    html += '</tr>';");
    client.println("  });");
    client.println("  html += '</table>';");
    client.println("  container.innerHTML = html;");
    client.println("}");
    
    client.println("function updateDevice(mac, isDevice, comment, hasStaticIP, ip) {");
    client.println("  fetch('/api/device', {");
    client.println("    method: 'POST',");
    client.println("    headers: { 'Content-Type': 'application/json' },");
    client.println("    body: JSON.stringify({");
    client.println("      mac: mac,");
    client.println("      isDevice: isDevice,");
    client.println("      comment: comment,");
    client.println("      hasStaticIP: hasStaticIP,");
    client.println("      ip: ip");
    client.println("    })");
    client.println("  }).then(() => loadDevices());");
    client.println("}");
    
    client.println("function setStaticIP(mac, currentIP) {");
    client.println("  const newIP = prompt('Enter static IP (192.168.");
    client.print(apConfig.subnet);
    client.println(".X):', currentIP);");
    client.println("  if (newIP) {");
    client.println("    fetch('/api/device', {");
    client.println("      method: 'POST',");
    client.println("      headers: { 'Content-Type': 'application/json' },");
    client.println("      body: JSON.stringify({");
    client.println("        mac: mac,");
    client.println("        hasStaticIP: true,");
    client.println("        ip: newIP");
    client.println("      })");
    client.println("    }).then(() => loadDevices());");
    client.println("  }");
    client.println("}");
    
    client.println("function saveAPSettings() {");
    client.println("  const subnet = document.getElementById('subnet').value;");
    client.println("  const ssid = document.getElementById('ssid').value;");
    client.println("  const password = document.getElementById('password').value;");
    client.println("  fetch('/api/ap', {");
    client.println("    method: 'POST',");
    client.println("    headers: { 'Content-Type': 'application/json' },");
    client.println("    body: JSON.stringify({");
    client.println("      subnet: parseInt(subnet),");
    client.println("      ssid: ssid,");
    client.println("      password: password");
    client.println("    })");
    client.println("  }).then(() => alert('AP settings saved. AP will restart.'));");
    client.println("}");
    
    client.println("function startScan() {");
    client.println("  fetch('/api/scan', { method: 'POST' })");
    client.println("    .then(() => {");
    client.println("      alert('Scan started. Results will appear shortly.');");
    client.println("      setTimeout(loadDevices, 5000);");
    client.println("    });");
    client.println("}");
    
    client.println("// Load devices on page load");
    client.println("window.onload = loadDevices;");
    client.println("</script>");
    
    client.println("</div>");
    client.println("</body>");
    client.println("</html>");
    client.stop();
}

int findDeviceByMAC(const char* mac) {
    for (int i = 0; i < deviceCount; i++) {
        if (strcmp(devices[i].mac, mac) == 0) {
            return i;
        }
    }
    return -1;
}

void scanNetwork() {
    scanInProgress = true;
    Serial.println("Scanning network...");
    
    // Получаем список подключенных станций через WiFi API
    wifi_sta_list_t station_list;
    esp_wifi_ap_get_sta_list(&station_list);
    
    Serial.print("Found stations: ");
    Serial.println(station_list.num);
    
    for (int i = 0; i < station_list.num; i++) {
        wifi_sta_info_t station = station_list.sta[i];
        
        // Преобразуем MAC адрес в строку
        char mac[18];
        sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X",
                station.mac[0], station.mac[1], station.mac[2],
                station.mac[3], station.mac[4], station.mac[5]);
        
        Serial.print("Found device MAC: ");
        Serial.println(mac);
        
        // Проверяем, есть ли устройство в списке
        int index = findDeviceByMAC(mac);
        if (index == -1 && deviceCount < MAX_DEVICES) {
            index = deviceCount++;
            strcpy(devices[index].mac, mac);
            
            // Устанавливаем IP адрес по умолчанию
            devices[index].hasStaticIP = false;
            devices[index].ip = ipToUint32(IPAddress(192, 168, apConfig.subnet, 100 + i));
            
            devices[index].isDevice = false;
            devices[index].comment[0] = '\0';
            Serial.print("Added new device: ");
            Serial.println(mac);
        } else if (index != -1) {
            // Если нет статического IP, обновляем
            if (!devices[index].hasStaticIP) {
                devices[index].ip = ipToUint32(IPAddress(192, 168, apConfig.subnet, 100 + i));
            }
            Serial.print("Updated device: ");
            Serial.println(mac);
        }
    }
    
    lastScanTime = millis();
    scanInProgress = false;
    saveConfig();
    Serial.println("Scan completed");
}

void saveConfig() {
    int addr = 0;
    
    // Сохраняем настройки AP
    EEPROM.put(addr, apConfig);
    addr += sizeof(apConfig);
    
    // Сохраняем количество устройств
    EEPROM.put(addr, deviceCount);
    addr += sizeof(deviceCount);
    
    // Сохраняем каждое устройство
    for (int i = 0; i < deviceCount; i++) {
        EEPROM.put(addr, devices[i]);
        addr += sizeof(DeviceConfig);
    }
    
    EEPROM.commit();
    Serial.println("Configuration saved to EEPROM");
}

void loadConfig() {
    int addr = 0;
    
    // Загружаем настройки AP
    EEPROM.get(addr, apConfig);
    addr += sizeof(apConfig);
    
    // Проверяем, инициализированы ли настройки
    if (apConfig.subnet < 0 || apConfig.subnet > 255) {
        // Значения по умолчанию
        apConfig.subnet = 4;
        strcpy(apConfig.ssid, "ESP32_AP");
        strcpy(apConfig.password, "12345678");
        Serial.println("Loaded default AP config");
    } else {
        Serial.println("Loaded AP config from EEPROM");
    }
    
    Serial.print("AP SSID: ");
    Serial.println(apConfig.ssid);
    Serial.print("AP Subnet: 192.168.");
    Serial.print(apConfig.subnet);
    Serial.println(".0");
    
    // Загружаем количество устройств
    int savedDeviceCount;
    EEPROM.get(addr, savedDeviceCount);
    addr += sizeof(savedDeviceCount);
    
    // Проверяем корректность количества устройств
    if (savedDeviceCount >= 0 && savedDeviceCount <= MAX_DEVICES) {
        deviceCount = savedDeviceCount;
        Serial.print("Loaded devices: ");
        Serial.println(deviceCount);
        
        // Загружаем каждое устройство
        for (int i = 0; i < deviceCount; i++) {
            DeviceConfig loadedDevice;
            EEPROM.get(addr, loadedDevice);
            addr += sizeof(DeviceConfig);
            
            // Проверяем валидность MAC адреса
            if (strlen(loadedDevice.mac) > 0 && strlen(loadedDevice.mac) < 18) {
                devices[i] = loadedDevice;
                
                // Безопасный вывод информации об устройстве
                Serial.print("Device ");
                Serial.print(i);
                Serial.print(": MAC=");
                Serial.print(devices[i].mac);
                Serial.print(", IP=");
                
                // Используем безопасную функцию для преобразования IP
                IPAddress ip = uint32ToIP(devices[i].ip);
                Serial.print(ip);
                
                Serial.print(", isDevice=");
                Serial.print(devices[i].isDevice);
                Serial.print(", Comment=");
                Serial.println(devices[i].comment);
            } else {
                // Если MAC невалиден, пропускаем это устройство
                Serial.print("Skipping invalid device at index ");
                Serial.println(i);
                deviceCount--;
                i--;
            }
        }
    } else {
        // Если количество устройств некорректно, сбрасываем
        deviceCount = 0;
        Serial.println("Invalid device count, resetting to 0");
    }
}
