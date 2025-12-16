#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>

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
};

const int MAX_CLIENTS = 10;
ClientInfo clients[MAX_CLIENTS];
int clientCount = 0;


void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP8266 Шлюз</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
    .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; text-align: center; }
    .nav { text-align: center; margin: 20px 0; }
    .nav a { margin: 0 10px; padding: 10px 20px; background: #007cba; color: white; text-decoration: none; border-radius: 5px; }
    .nav a:hover { background: #005a87; }
    .info-card { background: #e8f4fd; padding: 15px; margin: 15px 0; border-radius: 5px; border-left: 4px solid #007cba; }
    table { width: 100%; border-collapse: collapse; margin: 20px 0; }
    th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }
    th { background-color: #007cba; color: white; }
    tr:hover { background-color: #f5f5f5; }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP8266 Точка Доступа</h1>
    
    <div class="nav">
      <a href="/">Главная</a>
      <a href="/clients">Подключенные устройства</a>
      <a href="/scan">Сканировать сеть</a>
    </div>
    
    <div class="info-card">
      <h3>Информация о точке доступа:</h3>
      <p><strong>SSID:</strong> )rawliteral" + String(ssid) + R"rawliteral(</p>
      <p><strong>IP адрес шлюза:</strong> )rawliteral" + local_ip.toString() + R"rawliteral(</p>
      <p><strong>Маска подсети:</strong> )rawliteral" + subnet.toString() + R"rawliteral(</p>
      <p><strong>Количество подключенных клиентов:</strong> )rawliteral" + String(clientCount) + R"rawliteral(</p>
    </div>
    
    <div>
      <h3>Быстрые ссылки:</h3>
      <p><a href="http://192.168.5.2" target="_blank">Веб-сервер клиента (192.168.5.2)</a></p>
    </div>
  </div>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleClients() {
  String html = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Подключенные устройства</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
    .container { max-width: 1000px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; text-align: center; }
    .nav { text-align: center; margin: 20px 0; }
    .nav a { margin: 0 10px; padding: 10px 20px; background: #007cba; color: white; text-decoration: none; border-radius: 5px; }
    .nav a:hover { background: #005a87; }
    table { width: 100%; border-collapse: collapse; margin: 20px 0; }
    th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }
    th { background-color: #007cba; color: white; }
    tr:hover { background-color: #f5f5f5; }
    .refresh-btn { background: #28a745; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; margin: 10px 0; }
    .refresh-btn:hover { background: #218838; }
  </style>
  <script>
    function refreshPage() {
      location.reload();
    }
    setTimeout(function() {
      location.reload();
    }, 10000); // Автообновление каждые 10 секунд
  </script>
</head>
<body>
  <div class="container">
    <h1>Подключенные устройства</h1>
    
    <div class="nav">
      <a href="/">Главная</a>
      <a href="/clients">Подключенные устройства</a>
      <a href="/scan">Сканировать сеть</a>
    </div>
    
    <button class="refresh-btn" onclick="refreshPage()">Обновить</button>
    
    <table>
      <thead>
        <tr>
          <th>#</th>
          <th>IP Адрес</th>
          <th>MAC Адрес</th>
          <th>Имя хоста</th>
        </tr>
      </thead>
      <tbody>
)rawliteral";

  for (int i = 0; i < clientCount; i++) {
    html += "<tr>";
    html += "<td>" + String(i + 1) + "</td>";
    html += "<td>" + clients[i].ip.toString() + "</td>";
    html += "<td>" + clients[i].mac + "</td>";
    html += "<td>" + clients[i].hostname + "</td>";
    html += "</tr>";
  }

  if (clientCount == 0) {
    html += "<tr><td colspan='4' style='text-align: center;'>Нет подключенных устройств</td></tr>";
  }

  html += R"rawliteral(
      </tbody>
    </table>
    
    <p><strong>Всего устройств:</strong> )rawliteral" + String(clientCount) + R"rawliteral(</p>
  </div>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}


void updateClientList() {
  clientCount = 0;
  
  // Получаем информацию о подключенных станциях
  struct station_info *station_list = wifi_softap_get_station_info();
  struct station_info *station = station_list;
  
  int i = 0;
  while (station != NULL && i < MAX_CLIENTS) {
    clients[i].ip = IPAddress(station->ip.addr);
    
    // Преобразуем MAC адрес в строку
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             station->bssid[0], station->bssid[1], station->bssid[2],
             station->bssid[3], station->bssid[4], station->bssid[5]);
    clients[i].mac = String(macStr);
    
    clients[i].hostname = "Unknown"; // ESP8266 не предоставляет hostname клиентов
    
    station = STAILQ_NEXT(station, next);
    i++;
  }
  
  clientCount = i;
  wifi_softap_free_station_info();
}



void handleScan() {
  updateClientList();
  handleClients();
}

void setup() {
  Serial.begin(115200);
  
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
  
  server.begin();
  Serial.println("HTTP сервер запущен");
}

void loop() {
  server.handleClient();
  updateClientList();
  delay(1000);
}
