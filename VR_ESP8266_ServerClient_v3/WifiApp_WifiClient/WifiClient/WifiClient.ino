#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// Подключение к точке доступа
const char* ssid = "ESP8266_AP";
const char* password = "12345678";

// Статический IP для клиента
IPAddress local_ip(192, 168, 5, 2);
IPAddress gateway(192, 168, 5, 1);
IPAddress subnet(255, 255, 255, 0);

ESP8266WebServer server(80);

void setup() {
  Serial.begin(115200);
  
  // Подключение к точке доступа
  WiFi.config(local_ip, gateway, subnet);
  WiFi.begin(ssid, password);
  
  Serial.print("Подключение к ");
  Serial.println(ssid);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nПодключение установлено!");
  Serial.print("IP адрес клиента: ");
  Serial.println(WiFi.localIP());
  
  // Настройка маршрутов веб-сервера
  server.on("/", handleRoot);
  server.on("/info", handleInfo);
  server.on("/control", handleControl);
  server.on("/led", handleLED);
  
  server.begin();
  Serial.println("HTTP сервер клиента запущен");
  
  // Настройка пина для демонстрации
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // LED выключен (инверсная логика)
}

void loop() {
  server.handleClient();
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP8266 Клиент</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; }
    .container { max-width: 800px; margin: 0 auto; background: rgba(255,255,255,0.1); padding: 30px; border-radius: 15px; backdrop-filter: blur(10px); }
    h1 { text-align: center; margin-bottom: 30px; text-shadow: 2px 2px 4px rgba(0,0,0,0.3); }
    .nav { text-align: center; margin: 30px 0; }
    .nav a { margin: 0 10px; padding: 12px 25px; background: rgba(255,255,255,0.2); color: white; text-decoration: none; border-radius: 25px; border: 2px solid rgba(255,255,255,0.3); transition: all 0.3s; }
    .nav a:hover { background: rgba(255,255,255,0.3); transform: translateY(-2px); }
    .card { background: rgba(255,255,255,0.15); padding: 20px; margin: 20px 0; border-radius: 10px; border-left: 5px solid rgba(255,255,255,0.5); }
    .btn { background: #ff6b6b; color: white; padding: 12px 25px; border: none; border-radius: 25px; cursor: pointer; margin: 10px 5px; transition: all 0.3s; font-size: 16px; }
    .btn:hover { background: #ff5252; transform: translateY(-2px); box-shadow: 0 5px 15px rgba(0,0,0,0.2); }
    .btn-on { background: #51cf66; }
    .btn-on:hover { background: #40c057; }
    .btn-off { background: #ff6b6b; }
    .btn-off:hover { background: #ff5252; }
    .status { padding: 10px; border-radius: 5px; text-align: center; font-weight: bold; margin: 10px 0; }
    .status-on { background: rgba(81, 207, 102, 0.3); }
    .status-off { background: rgba(255, 107, 107, 0.3); }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP8266 Веб-Сервер Клиента</h1>
    
    <div class="nav">
      <a href="/">Главная</a>
      <a href="/info">Информация</a>
      <a href="/control">Управление LED</a>
      <a href="http://192.168.5.1">Шлюз (192.168.5.1)</a>
    </div>
    
    <div class="card">
      <h2>Добро пожаловать!</h2>
      <p>Это демонстрационный веб-сервер, работающий на ESP8266 в режиме клиента.</p>
      <p>Устройство подключено к точке доступа и предоставляет веб-интерфейс для управления и мониторинга.</p>
    </div>
    
    <div class="card">
      <h2>Быстрое управление LED</h2>
      <div id="ledStatus" class="status status-off">LED: ВЫКЛЮЧЕН</div>
      <button class="btn btn-on" onclick="controlLED(1)">Включить LED</button>
      <button class="btn btn-off" onclick="controlLED(0)">Выключить LED</button>
    </div>
  </div>

  <script>
    function controlLED(state) {
      fetch('/led?state=' + state)
        .then(response => response.text())
        .then(data => {
          const statusDiv = document.getElementById('ledStatus');
          if(state == 1) {
            statusDiv.textContent = 'LED: ВКЛЮЧЕН';
            statusDiv.className = 'status status-on';
          } else {
            statusDiv.textContent = 'LED: ВЫКЛЮЧЕН';
            statusDiv.className = 'status status-off';
          }
        });
    }
    
    // Обновляем статус LED при загрузке страницы
    window.onload = function() {
      fetch('/led')
        .then(response => response.text())
        .then(data => {
          const statusDiv = document.getElementById('ledStatus');
          if(data.includes('ON')) {
            statusDiv.textContent = 'LED: ВКЛЮЧЕН';
            statusDiv.className = 'status status-on';
          } else {
            statusDiv.textContent = 'LED: ВЫКЛЮЧЕН';
            statusDiv.className = 'status status-off';
          }
        });
    }
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleInfo() {
  String html = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Информация о клиенте</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: #2c3e50; color: #ecf0f1; }
    .container { max-width: 800px; margin: 0 auto; background: #34495e; padding: 30px; border-radius: 10px; }
    h1 { text-align: center; color: #3498db; }
    .nav { text-align: center; margin: 30px 0; }
    .nav a { margin: 0 10px; padding: 10px 20px; background: #3498db; color: white; text-decoration: none; border-radius: 5px; }
    .nav a:hover { background: #2980b9; }
    .info-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin: 20px 0; }
    .info-item { background: #2c3e50; padding: 15px; border-radius: 5px; border-left: 4px solid #3498db; }
    .info-label { font-weight: bold; color: #bdc3c7; }
    .info-value { color: #ecf0f1; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Информация о клиенте</h1>
    
    <div class="nav">
      <a href="/">Главная</a>
      <a href="/info">Информация</a>
      <a href="/control">Управление LED</a>
      <a href="http://192.168.5.1">Шлюз</a>
    </div>
    
    <div class="info-grid">
      <div class="info-item">
        <div class="info-label">IP Адрес:</div>
        <div class="info-value">)rawliteral" + WiFi.localIP().toString() + R"rawliteral(</div>
      </div>
      <div class="info-item">
        <div class="info-label">MAC Адрес:</div>
        <div class="info-value">)rawliteral" + WiFi.macAddress() + R"rawliteral(</div>
      </div>
      <div class="info-item">
        <div class="info-label">SSID:</div>
        <div class="info-value">)rawliteral" + String(WiFi.SSID()) + R"rawliteral(</div>
      </div>
      <div class="info-item">
        <div class="info-label">Сила сигнала:</div>
        <div class="info-value">)rawliteral" + String(WiFi.RSSI()) + R"rawliteral( dBm</div>
      </div>
      <div class="info-item">
        <div class="info-label">Статус:</div>
        <div class="info-value">)rawliteral" + String(WiFi.status() == WL_CONNECTED ? "Подключено" : "Отключено") + R"rawliteral(</div>
      </div>
      <div class="info-item">
        <div class="info-label">Шлюз:</div>
        <div class="info-value">)rawliteral" + WiFi.gatewayIP().toString() + R"rawliteral(</div>
      </div>
      <div class="info-item">
        <div class="info-label">Маска подсети:</div>
        <div class="info-value">)rawliteral" + WiFi.subnetMask().toString() + R"rawliteral(</div>
      </div>
      <div class="info-item">
        <div class="info-label">DNS сервер:</div>
        <div class="info-value">)rawliteral" + WiFi.dnsIP().toString() + R"rawliteral(</div>
      </div>
    </div>
    
    <div style="text-align: center; margin-top: 30px;">
      <a href="/" class="nav a">Вернуться на главную</a>
    </div>
  </div>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleControl() {
  handleRoot(); // Перенаправляем на главную страницу
}

void handleLED() {
  if (server.hasArg("state")) {
    int state = server.arg("state").toInt();
    digitalWrite(LED_BUILTIN, state ? LOW : HIGH); // Инверсная логика
    
    String response = "LED " + String(state ? "ON" : "OFF");
    server.send(200, "text/plain", response);
  } else {
    // Возвращаем текущее состояние
    String state = digitalRead(LED_BUILTIN) ? "OFF" : "ON";
    server.send(200, "text/plain", "LED " + state);
  }
}
