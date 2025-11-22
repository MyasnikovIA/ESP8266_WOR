#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

// Настройки WiFi сети
const char* ssid = "ESP8266_AP";     // Замените на имя вашей WiFi сети
const char* password = "12345678"; // Замените на пароль вашей WiFi сети

// Создаем веб-сервер на порту 80
ESP8266WebServer server(80);

// Переменные для хранения состояния
int ledState = LOW;
unsigned long startTime = 0;
int visitorCount = 0;

// HTML страница
const char* htmlPage = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP8266 Demo</title>
  <style>
    body { 
      font-family: Arial, sans-serif; 
      text-align: center; 
      margin: 50px; 
      background-color: #f0f0f0;
    }
    .container { 
      background: white; 
      padding: 30px; 
      border-radius: 15px; 
      box-shadow: 0 0 10px rgba(0,0,0,0.1);
      max-width: 500px;
      margin: 0 auto;
    }
    h1 { color: #333; }
    .info { 
      background: #e7f3ff; 
      padding: 15px; 
      margin: 15px 0; 
      border-radius: 8px;
      border-left: 4px solid #2196F3;
    }
    .button { 
      background: #4CAF50; 
      color: white; 
      padding: 12px 24px; 
      border: none; 
      border-radius: 5px; 
      cursor: pointer; 
      font-size: 16px;
      margin: 5px;
    }
    .button:hover { background: #45a049; }
    .button-red { background: #f44336; }
    .button-red:hover { background: #da190b; }
    .status { 
      padding: 10px; 
      margin: 10px 0; 
      border-radius: 5px;
      font-weight: bold;
    }
    .led-on { background: #4CAF50; color: white; }
    .led-off { background: #666; color: white; }
    .wifi-status { 
      padding: 8px; 
      margin: 5px 0; 
      border-radius: 5px;
      font-size: 14px;
    }
    .connected { background: #d4edda; color: #155724; }
    .disconnected { background: #f8d7da; color: #721c24; }
  </style>
</head>
<body>
  <div class="container">
    <h1>🚀 ESP8266 WiFi Client</h1>
    
    <div class="wifi-status %WIFICLASS%">
      WiFi: %WIFISTATUS%
    </div>
    
    <div class="info">
      <h3>📊 Информация о системе</h3>
      <p><strong>Время работы:</strong> %UPTIME%</p>
      <p><strong>Посетителей:</strong> %VISITORS%</p>
      <p><strong>Статус LED:</strong> %LEDSTATUS%</p>
      <p><strong>IP адрес:</strong> %IPADDRESS%</p>
      <p><strong>SSID сети:</strong> %SSID%</p>
      <p><strong>Сила сигнала:</strong> %RSSI% dBm</p>
      <p><strong>Чип ID:</strong> %CHIPID%</p>
    </div>

    <div class="status %LEDCLASS%">
      LED: %LEDTEXT%
    </div>

    <div>
      <button class="button" onclick="location.href='/led/on'">🟢 Включить LED</button>
      <button class="button button-red" onclick="location.href='/led/off'">🔴 Выключить LED</button>
      <button class="button" onclick="location.href='/blink'">✨ Мигать LED</button>
    </div>

    <div style="margin-top: 20px;">
      <button class="button" onclick="location.href='/info'">ℹ️ Информация</button>
      <button class="button" onclick="location.href='/restart'">🔄 Перезагрузить</button>
      <button class="button" onclick="location.href='/wifi-scan'">📡 Сканировать WiFi</button>
    </div>

    <div style="margin-top: 30px; font-size: 14px; color: #666;">
      <p>ESP8266 WiFi Client | Версия 2.0</p>
    </div>
  </div>
</body>
</html>
)rawliteral";

// Функция для форматирования времени
String formatTime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  
  char buffer[50];
  if (days > 0) {
    snprintf(buffer, sizeof(buffer), "%luд %02lu:%02lu:%02lu", days, hours, minutes, seconds);
  } else {
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes, seconds);
  }
  return String(buffer);
}

// Функция для получения силы сигнала WiFi
String getWiFiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    return "Подключено";
  } else {
    return "Не подключено";
  }
}

String getWiFiRSSI() {
  if (WiFi.status() == WL_CONNECTED) {
    return String(WiFi.RSSI());
  } else {
    return "N/A";
  }
}

// Функция для обработки главной страницы
void handleRoot() {
  visitorCount++;
  
  String page = htmlPage;
  
  // Заменяем плейсхолдеры на реальные значения
  page.replace("%UPTIME%", formatTime(millis() - startTime));
  page.replace("%VISITORS%", String(visitorCount));
  page.replace("%LEDSTATUS%", ledState ? "ВКЛЮЧЕН" : "ВЫКЛЮЧЕН");
  page.replace("%IPADDRESS%", WiFi.localIP().toString());
  page.replace("%SSID%", WiFi.SSID());
  page.replace("%RSSI%", getWiFiRSSI());
  page.replace("%CHIPID%", String(ESP.getChipId()));
  page.replace("%LEDTEXT%", ledState ? "ВКЛЮЧЕН" : "ВЫКЛЮЧЕН");
  page.replace("%LEDCLASS%", ledState ? "led-on" : "led-off");
  page.replace("%WIFISTATUS%", getWiFiStatus() + " | " + WiFi.SSID());
  page.replace("%WIFICLASS%", WiFi.status() == WL_CONNECTED ? "connected" : "disconnected");
  
  server.send(200, "text/html", page);
}

// Включение LED
void handleLedOn() {
  ledState = HIGH;
  digitalWrite(LED_BUILTIN, ledState);
  server.send(200, "text/plain", "LED включен");
}

// Выключение LED
void handleLedOff() {
  ledState = LOW;
  digitalWrite(LED_BUILTIN, ledState);
  server.send(200, "text/plain", "LED выключен");
}

// Мигание LED
void handleBlink() {
  server.send(200, "text/plain", "LED мигает 5 раз");
  
  for(int i = 0; i < 10; i++) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(500);
  }
  digitalWrite(LED_BUILTIN, ledState);
}

// Информация о системе
void handleInfo() {
  String info = "Информация о системе:\n\n";
  info += "=== WiFi ===\n";
  info += "Статус: " + getWiFiStatus() + "\n";
  info += "SSID: " + WiFi.SSID() + "\n";
  info += "IP адрес: " + WiFi.localIP().toString() + "\n";
  info += "MAC адрес: " + WiFi.macAddress() + "\n";
  info += "Сила сигнала: " + getWiFiRSSI() + " dBm\n";
  info += "Шлюз: " + WiFi.gatewayIP().toString() + "\n";
  info += "DNS: " + WiFi.dnsIP().toString() + "\n\n";
  
  info += "=== Система ===\n";
  info += "Время работы: " + formatTime(millis() - startTime) + "\n";
  info += "Посетителей: " + String(visitorCount) + "\n";
  info += "Статус LED: " + String(ledState ? "ВКЛЮЧЕН" : "ВЫКЛЮЧЕН") + "\n";
  info += "ID чипа: " + String(ESP.getChipId()) + "\n";
  info += "Частота CPU: " + String(ESP.getCpuFreqMHz()) + " MHz\n";
  info += "Свободная память: " + String(ESP.getFreeHeap()) + " байт\n";
  info += "Размер Flash: " + String(ESP.getFlashChipSize()) + " байт\n";
  
  server.send(200, "text/plain", info);
}

// Сканирование WiFi сетей
void handleWiFiScan() {
  String page = "<html><head><meta charset='UTF-8'><title>Сканирование WiFi</title></head><body>";
  page += "<h1>📡 Доступные WiFi сети</h1>";
  page += "<a href='/'>← Назад</a><br><br>";
  
  int n = WiFi.scanComplete();
  if (n == -2) {
    // Сканирование еще не запускалось
    WiFi.scanNetworks(true);
    page += "Сканирование запущено...<br>";
    page += "<script>setTimeout(function(){ location.reload(); }, 3000);</script>";
  } else if (n == -1) {
    // Сканирование выполняется
    page += "Сканирование выполняется...<br>";
    page += "<script>setTimeout(function(){ location.reload(); }, 3000);</script>";
  } else if (n == 0) {
    page += "Сети не найдены";
  } else {
    page += "Найдено сетей: " + String(n) + "<br><br>";
    page += "<table border='1' cellpadding='5'>";
    page += "<tr><th>SSID</th><th>Сигнал</th><th>Защита</th><th>Канал</th></tr>";
    
    for (int i = 0; i < n; ++i) {
      page += "<tr>";
      page += "<td>" + WiFi.SSID(i) + "</td>";
      page += "<td>" + String(WiFi.RSSI(i)) + " dBm</td>";
      page += "<td>" + String((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "Открытая" : "Защищенная") + "</td>";
      page += "<td>" + String(WiFi.channel(i)) + "</td>";
      page += "</tr>";
    }
    page += "</table>";
    
    // Сбрасываем сканирование для следующего раза
    WiFi.scanDelete();
  }
  
  page += "</body></html>";
  server.send(200, "text/html", page);
}

// Перезагрузка
void handleRestart() {
  server.send(200, "text/html", "<html><body><h1>Перезагрузка...</h1><p>ESP8266 перезагрузится через 3 секунды</p></body></html>");
  delay(3000);
  ESP.restart();
}

// Обработка несуществующих страниц
void handleNotFound() {
  String message = "Страница не найдена\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nДоступные страницы:\n";
  message += "/ - Главная страница\n";
  message += "/led/on - Включить LED\n";
  message += "/led/off - Выключить LED\n";
  message += "/blink - Мигать LED\n";
  message += "/info - Информация о системе\n";
  message += "/wifi-scan - Сканировать WiFi\n";
  message += "/restart - Перезагрузка\n";
  
  server.send(404, "text/plain", message);
}

void setup() {
  // Инициализация последовательного порта
  Serial.begin(115200);
  delay(1000);
  
  // Настройка встроенного LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // LED включен (активный низкий уровень)
  ledState = LOW;
  
  // Подключение к WiFi
  Serial.println();
  Serial.println("Подключение к WiFi...");
  Serial.print("SSID: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  // Ожидание подключения
  Serial.print("Подключение");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // Мигаем LED во время подключения
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi подключен!");
    Serial.print("IP адрес: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_BUILTIN, LOW); // Выключаем LED после подключения
  } else {
    Serial.println("Не удалось подключиться к WiFi!");
    // Можно продолжить работу в режиме AP
    WiFi.softAP("ESP8266_Fallback", "12345678");
    Serial.print("Запущен резервный AP. IP: ");
    Serial.println(WiFi.softAPIP());
  }
  
  // Настройка веб-сервера
  server.on("/", handleRoot);
  server.on("/led/on", handleLedOn);
  server.on("/led/off", handleLedOff);
  server.on("/blink", handleBlink);
  server.on("/info", handleInfo);
  server.on("/wifi-scan", handleWiFiScan);
  server.on("/restart", handleRestart);
  server.onNotFound(handleNotFound);
  
  // Запуск сервера
  server.begin();
  Serial.println("HTTP сервер запущен");
  
  // Запоминаем время старта
  startTime = millis();
  
  Serial.println("Готово! Откройте в браузере ваш IP адрес:");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Обработка клиентов
  server.handleClient();
}
