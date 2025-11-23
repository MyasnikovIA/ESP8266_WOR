
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <ArduinoJson.h>

// Настройки WiFi сети
const char* ssid = "ESP8266_AP";
const char* password = "12345678";

// Создаем веб-сервер на порту 80
ESP8266WebServer server(80);
// WebSocket сервер на порту 81
WebSocketsServer webSocket = WebSocketsServer(81);

// Переменные для хранения состояния
int ledState = LOW;
unsigned long startTime = 0;
int visitorCount = 0;

// MPU6050 сенсор
Adafruit_MPU6050 mpu;
bool mpuConnected = false;

// Данные сенсора
float pitch = 0, roll = 0, yaw = 0;
float smoothedPitch = 0, smoothedRoll = 0, smoothedYaw = 0;
const float smoothingFactor = 0.3;

// Фильтр и калибровка
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
bool calibrated = false;
unsigned long lastTime = 0;
unsigned long calibrationStart = 0;
const unsigned long calibrationTime = 3000;

// Нулевая точка
float zeroPitch = 0, zeroRoll = 0, zeroYaw = 0;
bool zeroSet = false;

// Авто-калибровка
bool autoCalibrationEnabled = true;
const unsigned long AUTO_CALIBRATION_INTERVAL = 60000;
unsigned long lastAutoCalibration = 0;

// Управление отправкой данных
unsigned long lastDataSend = 0;
const unsigned long DATA_SEND_INTERVAL = 50;

// HTML страница с 3D визуализацией
const char* htmlPage = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP8266 MPU6050 Demo</title>
  <style>
    body { 
      font-family: Arial, sans-serif; 
      margin: 0; 
      padding: 20px; 
      background-color: #f0f0f0;
      overflow-x: hidden;
    }
    .container { 
      max-width: 1200px; 
      margin: 0 auto; 
      background: white; 
      padding: 20px; 
      border-radius: 15px; 
      box-shadow: 0 0 10px rgba(0,0,0,0.1);
    }
    h1 { color: #333; text-align: center; }
    .dashboard {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 20px;
      margin: 20px 0;
    }
    .info { 
      background: #e7f3ff; 
      padding: 15px; 
      border-radius: 8px;
      border-left: 4px solid #2196F3;
    }
    .sensor-data {
      background: #fff3cd;
      padding: 15px;
      border-radius: 8px;
      border-left: 4px solid #ffc107;
    }
    .button { 
      background: #4CAF50; 
      color: white; 
      padding: 10px 15px; 
      border: none; 
      border-radius: 5px; 
      cursor: pointer; 
      font-size: 14px;
      margin: 5px;
    }
    .button:hover { background: #45a049; }
    .button-red { background: #f44336; }
    .button-red:hover { background: #da190b; }
    .button-blue { background: #2196F3; }
    .button-blue:hover { background: #1976D2; }
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
    #visualization {
      width: 100%;
      height: 400px;
      background: #2c3e50;
      border-radius: 8px;
      margin: 20px 0;
      position: relative;
      overflow: hidden;
    }
    .control-panel {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 10px;
      margin: 20px 0;
    }
    .data-display {
      font-family: 'Courier New', monospace;
      background: #f8f9fa;
      padding: 10px;
      border-radius: 5px;
      margin: 5px 0;
    }
    .websocket-status {
      padding: 10px;
      border-radius: 5px;
      margin: 10px 0;
      text-align: center;
      font-weight: bold;
    }
    .ws-connected { background: #d4edda; color: #155724; }
    .ws-disconnected { background: #f8d7da; color: #721c24; }
  </style>
</head>
<body>
  <div class="container">
    <h1>🚀 ESP8266 MPU6050 Sensor Demo</h1>
    
    <div class="wifi-status %WIFICLASS%">
      WiFi: %WIFISTATUS%
    </div>

    <div class="websocket-status" id="wsStatus">
      WebSocket: Connecting...
    </div>
    
    <div class="dashboard">
      <div class="info">
        <h3>📊 Системная информация</h3>
        <p><strong>Время работы:</strong> %UPTIME%</p>
        <p><strong>Посетителей:</strong> %VISITORS%</p>
        <p><strong>Статус LED:</strong> %LEDSTATUS%</p>
        <p><strong>IP адрес:</strong> %IPADDRESS%</p>
        <p><strong>SSID сети:</strong> %SSID%</p>
        <p><strong>Сила сигнала:</strong> %RSSI% dBm</p>
        <p><strong>Чип ID:</strong> %CHIPID%</p>
      </div>

      <div class="sensor-data">
        <h3>🎯 Данные MPU6050</h3>
        <div class="data-display">
          <div>Pitch: <span id="pitch">0.00</span>°</div>
          <div>Roll: <span id="roll">0.00</span>°</div>
          <div>Yaw: <span id="yaw">0.00</span>°</div>
        </div>
        <div class="data-display">
          <div>Относительный Pitch: <span id="relPitch">0.00</span>°</div>
          <div>Относительный Roll: <span id="relRoll">0.00</span>°</div>
          <div>Относительный Yaw: <span id="relYaw">0.00</span>°</div>
        </div>
        <div class="data-display">
          <div>Статус калибровки: <span id="calibrationStatus">Калибруется...</span></div>
          <div>Авто-калибровка: <span id="autoCalStatus">Включена</span></div>
          <div>Нулевая точка: <span id="zeroStatus">Не установлена</span></div>
        </div>
      </div>
    </div>

    <div id="visualization">
      <canvas id="cubeCanvas" width="800" height="400"></canvas>
    </div>

    <div class="control-panel">
      <div>
        <h4>Управление LED</h4>
        <button class="button" onclick="sendCommand('ledOn')">🟢 Включить LED</button>
        <button class="button button-red" onclick="sendCommand('ledOff')">🔴 Выключить LED</button>
        <button class="button" onclick="sendCommand('blink')">✨ Мигать LED</button>
      </div>
      
      <div>
        <h4>Управление сенсором</h4>
        <button class="button button-blue" onclick="sendCommand('setZero')">🎯 Установить нулевую точку</button>
        <button class="button" onclick="sendCommand('resetZero')">🔄 Сбросить нулевую точку</button>
        <button class="button" onclick="sendCommand('resetYaw')">↩️ Сбросить Yaw</button>
        <button class="button" onclick="sendCommand('resetAngles')">🔄 Сбросить все углы</button>
      </div>
      
      <div>
        <h4>Калибровка</h4>
        <button class="button" onclick="sendCommand('recalibrate')">⚙️ Перекалибровать</button>
        <button class="button" id="autoCalBtn" onclick="toggleAutoCalibration()">🔴 Выключить авто-калибровку</button>
      </div>
      
      <div>
        <h4>Система</h4>
        <button class="button" onclick="location.href='/info'">ℹ️ Информация</button>
        <button class="button" onclick="sendCommand('restart')">🔄 Перезагрузить</button>
      </div>
    </div>

    <div class="status %LEDCLASS%">
      LED: %LEDTEXT%
    </div>

    <div style="margin-top: 30px; font-size: 14px; color: #666; text-align: center;">
      <p>ESP8266 MPU6050 Sensor | Версия 3.0</p>
    </div>
  </div>

  <script>
    let ws = null;
    let cubeCanvas, ctx;
    let sensorData = { pitch: 0, roll: 0, yaw: 0, relPitch: 0, relRoll: 0, relYaw: 0 };

    function connectWebSocket() {
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      const wsUrl = `${protocol}//${window.location.hostname}:81`;
      
      ws = new WebSocket(wsUrl);
      
      ws.onopen = function() {
        document.getElementById('wsStatus').className = 'websocket-status ws-connected';
        document.getElementById('wsStatus').textContent = 'WebSocket: Connected';
        console.log('WebSocket connected');
      };
      
      ws.onclose = function() {
        document.getElementById('wsStatus').className = 'websocket-status ws-disconnected';
        document.getElementById('wsStatus').textContent = 'WebSocket: Disconnected';
        console.log('WebSocket disconnected');
        // Попытка переподключения через 3 секунды
        setTimeout(connectWebSocket, 3000);
      };
      
      ws.onmessage = function(event) {
        try {
          const data = JSON.parse(event.data);
          handleWebSocketMessage(data);
        } catch (e) {
          console.error('Error parsing WebSocket message:', e);
        }
      };
      
      ws.onerror = function(error) {
        console.error('WebSocket error:', error);
      };
    }

    function handleWebSocketMessage(data) {
      if (data.type === 'sensorData') {
        // Обновление данных сенсора
        sensorData = data;
        updateSensorDisplay();
        drawCube();
      } else if (data.type === 'status') {
        console.log('Status:', data.message);
        alert(data.message);
      } else if (data.type === 'calibrationStatus') {
        document.getElementById('calibrationStatus').textContent = data.calibrated ? 'Готов' : 'Калибруется...';
      } else if (data.type === 'autoCalibrationStatus') {
        const btn = document.getElementById('autoCalBtn');
        btn.textContent = data.enabled ? '🔴 Выключить авто-калибровку' : '🟢 Включить авто-калибровку';
        document.getElementById('autoCalStatus').textContent = data.enabled ? 'Включена' : 'Выключена';
      } else if (data.type === 'zeroInfo') {
        document.getElementById('zeroStatus').textContent = 'Установлена';
      } else if (data.type === 'zeroReset') {
        document.getElementById('zeroStatus').textContent = 'Не установлена';
      }
    }

    function updateSensorDisplay() {
      document.getElementById('pitch').textContent = sensorData.pitch.toFixed(2);
      document.getElementById('roll').textContent = sensorData.roll.toFixed(2);
      document.getElementById('yaw').textContent = sensorData.yaw.toFixed(2);
      document.getElementById('relPitch').textContent = sensorData.relPitch.toFixed(2);
      document.getElementById('relRoll').textContent = sensorData.relRoll.toFixed(2);
      document.getElementById('relYaw').textContent = sensorData.relYaw.toFixed(2);
    }

    function sendCommand(command) {
      if (ws && ws.readyState === WebSocket.OPEN) {
        let message = '';
        switch(command) {
          case 'ledOn':
          case 'ledOff':
          case 'blink':
          case 'restart':
            message = JSON.stringify({ type: command });
            break;
          case 'setZero':
          case 'resetZero':
          case 'resetYaw':
          case 'recalibrate':
            message = JSON.stringify({ type: command });
            break;
        }
        if (message) {
          ws.send(message);
        }
      } else {
        alert('WebSocket не подключен!');
      }
    }

    function toggleAutoCalibration() {
      if (ws && ws.readyState === WebSocket.OPEN) {
        const btn = document.getElementById('autoCalBtn');
        const currentlyEnabled = btn.textContent.includes('Выключить');
        ws.send(JSON.stringify({ 
          type: 'setAutoCalibration', 
          enable: !currentlyEnabled 
        }));
      }
    }

    // 3D визуализация куба
    function initCube() {
      cubeCanvas = document.getElementById('cubeCanvas');
      ctx = cubeCanvas.getContext('2d');
      
      // Адаптивный размер canvas
      function resizeCanvas() {
        const container = document.getElementById('visualization');
        cubeCanvas.width = container.clientWidth;
        cubeCanvas.height = container.clientHeight;
      }
      
      window.addEventListener('resize', resizeCanvas);
      resizeCanvas();
    }

    function drawCube() {
      if (!ctx) return;
      
      const width = cubeCanvas.width;
      const height = cubeCanvas.height;
      const centerX = width / 2;
      const centerY = height / 2;
      const size = Math.min(width, height) * 0.2;
      
      // Очистка canvas
      ctx.fillStyle = '#2c3e50';
      ctx.fillRect(0, 0, width, height);
      
      // Используем абсолютные углы вместо относительных
      // Преобразование углов в радианы (убираем нормализацию)
      const pitchRad = (sensorData.pitch % 360) * Math.PI / 180;
      const rollRad = (sensorData.roll % 360) * Math.PI / 180;
      const yawRad = (sensorData.yaw % 360) * Math.PI / 180;
      
      // Вершины куба
      const vertices = [
        { x: -size, y: -size, z: -size },
        { x: size, y: -size, z: -size },
        { x: size, y: size, z: -size },
        { x: -size, y: size, z: -size },
        { x: -size, y: -size, z: size },
        { x: size, y: -size, z: size },
        { x: size, y: size, z: size },
        { x: -size, y: size, z: size }
      ];
      
      // Проекция 3D в 2D
      function project(point) {
        // Поворот по осям
        let x = point.x;
        let y = point.y;
        let z = point.z;
        
        // Поворот вокруг X (pitch)
        const cosPitch = Math.cos(pitchRad);
        const sinPitch = Math.sin(pitchRad);
        let y1 = y * cosPitch - z * sinPitch;
        let z1 = y * sinPitch + z * cosPitch;
        
        // Поворот вокруг Y (roll)
        const cosRoll = Math.cos(rollRad);
        const sinRoll = Math.sin(rollRad);
        let x1 = x * cosRoll + z1 * sinRoll;
        let z2 = -x * sinRoll + z1 * cosRoll;
        
        // Поворот вокруг Z (yaw)
        const cosYaw = Math.cos(yawRad);
        const sinYaw = Math.sin(yawRad);
        let x2 = x1 * cosYaw - y1 * sinYaw;
        let y2 = x1 * sinYaw + y1 * cosYaw;
        
        // Перспективная проекция
        const perspective = 500;
        const scale = perspective / (perspective + z2);
        
        return {
          x: centerX + x2 * scale,
          y: centerY + y2 * scale
        };
      }
      
      // Проецируем все вершины
      const projected = vertices.map(project);
      
      // Рисуем грани
      const faces = [
        [0, 1, 2, 3], // задняя
        [4, 5, 6, 7], // передняя
        [0, 4, 7, 3], // левая
        [1, 5, 6, 2], // правая
        [0, 1, 5, 4], // нижняя
        [3, 2, 6, 7]  // верхняя
      ];
      
      const colors = ['#e74c3c', '#3498db', '#2ecc71', '#f39c12', '#9b59b6', '#1abc9c'];
      
      faces.forEach((face, index) => {
        ctx.fillStyle = colors[index];
        ctx.strokeStyle = '#34495e';
        ctx.lineWidth = 2;
        
        ctx.beginPath();
        ctx.moveTo(projected[face[0]].x, projected[face[0]].y);
        for (let i = 1; i < face.length; i++) {
          ctx.lineTo(projected[face[i]].x, projected[face[i]].y);
        }
        ctx.closePath();
        ctx.fill();
        ctx.stroke();
      });
      
      // Рисуем оси
      drawAxes();
    }

    function drawAxes() {
      const length = 100;
      const origin = { x: 0, y: 0, z: 0 };
      const xAxis = { x: length, y: 0, z: 0 };
      const yAxis = { x: 0, y: length, z: 0 };
      const zAxis = { x: 0, y: 0, z: length };
      
      const projOrigin = project(origin);
      const projX = project(xAxis);
      const projY = project(yAxis);
      const projZ = project(zAxis);
      
      // Ось X (красная)
      ctx.strokeStyle = '#e74c3c';
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(projOrigin.x, projOrigin.y);
      ctx.lineTo(projX.x, projX.y);
      ctx.stroke();
      
      // Ось Y (зеленая)
      ctx.strokeStyle = '#2ecc71';
      ctx.beginPath();
      ctx.moveTo(projOrigin.x, projOrigin.y);
      ctx.lineTo(projY.x, projY.y);
      ctx.stroke();
      
      // Ось Z (синяя)
      ctx.strokeStyle = '#3498db';
      ctx.beginPath();
      ctx.moveTo(projOrigin.x, projOrigin.y);
      ctx.lineTo(projZ.x, projZ.y);
      ctx.stroke();
    }

    // Функция проекции для осей (дублирует основную функцию проекции)
    function project(point) {
      const pitchRad = sensorData.relPitch * Math.PI / 180;
      const rollRad = sensorData.relRoll * Math.PI / 180;
      const yawRad = sensorData.relYaw * Math.PI / 180;
      
      const width = cubeCanvas.width;
      const height = cubeCanvas.height;
      const centerX = width / 2;
      const centerY = height / 2;
      
      let x = point.x;
      let y = point.y;
      let z = point.z;
      
      const cosPitch = Math.cos(pitchRad);
      const sinPitch = Math.sin(pitchRad);
      let y1 = y * cosPitch - z * sinPitch;
      let z1 = y * sinPitch + z * cosPitch;
      
      const cosRoll = Math.cos(rollRad);
      const sinRoll = Math.sin(rollRad);
      let x1 = x * cosRoll + z1 * sinRoll;
      let z2 = -x * sinRoll + z1 * cosRoll;
      
      const cosYaw = Math.cos(yawRad);
      const sinYaw = Math.sin(yawRad);
      let x2 = x1 * cosYaw - y1 * sinYaw;
      let y2 = x1 * sinYaw + y1 * cosYaw;
      
      const perspective = 500;
      const scale = perspective / (perspective + z2);
      
      return {
        x: centerX + x2 * scale,
        y: centerY + y2 * scale
      };
    }

    // Инициализация при загрузке страницы
    document.addEventListener('DOMContentLoaded', function() {
      initCube();
      connectWebSocket();
      // Запуск анимации
      setInterval(drawCube, 50);
    });
  </script>
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

// Инициализация MPU6050
bool initializeMPU6050() {
  Serial.println("🔍 Инициализация MPU6050...");
  
  if (mpu.begin()) {
    // Настройка MPU6050
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setGyroRange(MPU6050_RANGE_250_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
    
    Serial.println("✅ MPU6050 найден и инициализирован");
    return true;
  } else {
    Serial.println("❌ MPU6050 не найден!");
    return false;
  }
}

// Обработка данных сенсора
void processSensorData() {
  if (!calibrated) {
    calibrateGyro();
    return;
  }
  
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    Serial.println("Ошибка чтения данных MPU6050");
    return;
  }
  
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  if (lastTime == 0) {
    deltaTime = 0.01;
  }
  lastTime = currentTime;
  
  // Компенсация смещения гироскопа
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  // ТОЛЬКО интеграция гироскопа (без ограничений)
  pitch += gyroX * deltaTime * 180.0 / PI;
  roll += gyroY * deltaTime * 180.0 / PI;
  yaw += gyroZ * deltaTime * 180.0 / PI;
  
  // Убрать комплементарный фильтр с акселерометром для pitch и roll
  // Акселерометр ограничивает углы ±180°, поэтому используем только гироскоп
  // float alpha = 0.96;
  // pitch = alpha * pitch + (1.0 - alpha) * accelPitch;
  // roll = alpha * roll + (1.0 - alpha) * accelRoll;
  
  // Сглаживание для отображения
  smoothedPitch = smoothedPitch * (1 - smoothingFactor) + pitch * smoothingFactor;
  smoothedRoll = smoothedRoll * (1 - smoothingFactor) + roll * smoothingFactor;
  smoothedYaw = smoothedYaw * (1 - smoothingFactor) + yaw * smoothingFactor;
  
  // Отладочный вывод для отслеживания дрейфа
  static unsigned long lastDebug = 0;
  if (currentTime - lastDebug > 10000) { // Каждые 10 секунд
    lastDebug = currentTime;
    Serial.printf("📊 Текущие углы - Pitch: %.1f°, Roll: %.1f°, Yaw: %.1f°\n", pitch, roll, yaw);
  }
}


// Калибровка гироскопа
void calibrateGyro() {
  if (calibrated) return;
  
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  static int sampleCount = 0;
  static float sumX = 0, sumY = 0, sumZ = 0;
  
  if (millis() - calibrationStart < calibrationTime) {
    sumX += g.gyro.x;
    sumY += g.gyro.y;
    sumZ += g.gyro.z;
    sampleCount++;
    
    if (sampleCount % 50 == 0) {
      int progress = (millis() - calibrationStart) * 100 / calibrationTime;
      float currentOffsetX = sumX / sampleCount;
      float currentOffsetY = sumY / sampleCount;
      float currentOffsetZ = sumZ / sampleCount;
      Serial.printf("Калибровка: %d%%, Offsets: X:%.6f, Y:%.6f, Z:%.6f\n", 
                   progress, currentOffsetX, currentOffsetY, currentOffsetZ);
    }
  } else {
    gyroOffsetX = sumX / sampleCount;
    gyroOffsetY = sumY / sampleCount;
    gyroOffsetZ = sumZ / sampleCount;
    calibrated = true;
    
    // Сброс углов после калибровки
    pitch = 0;
    roll = 0;
    yaw = 0;
    smoothedPitch = 0;
    smoothedRoll = 0;
    smoothedYaw = 0;
    
    Serial.println("✅ Калибровка гироскопа завершена!");
    Serial.printf("Финальные смещения - X:%.6f, Y:%.6f, Z:%.6f\n", gyroOffsetX, gyroOffsetY, gyroOffsetZ);
    Serial.printf("Обработано samples: %d\n", sampleCount);
    
    // Уведомление клиентов
    String statusMsg = "{\"type\":\"status\",\"message\":\"Калибровка завершена\"}";
    webSocket.broadcastTXT(statusMsg);
  }
}
// Функция для принудительного сброса углов
void resetAllAngles() {
  pitch = 0;
  roll = 0;
  yaw = 0;
  smoothedPitch = 0;
  smoothedRoll = 0;
  smoothedYaw = 0;
  Serial.println("🔄 Все углы сброшены в 0");
}

// Отправка данных сенсора через WebSocket
void sendSensorData() {
  if (webSocket.connectedClients() == 0) return;
  
  // Расчет относительных углов (теперь без нормализации)
  float relPitch = calculateRelativeAngle(smoothedPitch, zeroPitch);
  float relRoll = calculateRelativeAngle(smoothedRoll, zeroRoll);
  float relYaw = calculateRelativeAngle(smoothedYaw, zeroYaw);
  
  // Создание JSON данных
  String json = "{";
  json += "\"type\":\"sensorData\",";
  json += "\"pitch\":" + String(smoothedPitch, 2) + ",";
  json += "\"roll\":" + String(smoothedRoll, 2) + ",";
  json += "\"yaw\":" + String(smoothedYaw, 2) + ",";
  json += "\"relPitch\":" + String(relPitch, 2) + ",";
  json += "\"relRoll\":" + String(relRoll, 2) + ",";
  json += "\"relYaw\":" + String(relYaw, 2) + ",";
  json += "\"zeroSet\":" + String(zeroSet ? "true" : "false") + ",";
  json += "\"calibrated\":" + String(calibrated ? "true" : "false") + ",";
  json += "\"autoCalibration\":" + String(autoCalibrationEnabled ? "true" : "false") + ",";
  json += "\"signal\":" + String(WiFi.RSSI()) + ",";
  json += "\"timestamp\":" + String(millis());
  json += "}";
  
  // Отправка всем подключенным клиентам
  webSocket.broadcastTXT(json);
}

// Расчет относительного угла
float calculateRelativeAngle(float absoluteAngle, float zeroAngle) {
  float relative = absoluteAngle - zeroAngle;
  return relative;
}

// Установка нулевой точки
void setZeroPoint() {
  zeroPitch = smoothedPitch;
  zeroRoll = smoothedRoll;
  zeroYaw = smoothedYaw;
  zeroSet = true;
  
  Serial.printf("💾 Нулевая точка установлена - Pitch:%.1f° Roll:%.1f° Yaw:%.1f°\n", 
               zeroPitch, zeroRoll, zeroYaw);
}

// Сброс нулевой точки
void resetZeroPoint() {
  zeroPitch = 0;
  zeroRoll = 0;
  zeroYaw = 0;
  zeroSet = false;
  
  Serial.println("🔄 Нулевая точка сброшена");
}

// Сброс Yaw
void resetYaw() {
  yaw = 0;
  smoothedYaw = 0;
  
  Serial.println("🔄 Yaw сброшен");
}

// Перекалибровка
void recalibrate() {
  calibrated = false;
  pitch = roll = yaw = 0;
  calibrationStart = millis();
  
  Serial.println("🔄 Перекалибровка запущена");
}

// Установка авто-калибровки
void setAutoCalibration(bool enable) {
  autoCalibrationEnabled = enable;
  if (enable) {
    lastAutoCalibration = millis();
  }
  
  Serial.printf("⚙️ Авто-калибровка %s\n", enable ? "включена" : "выключена");
}

// Обработчик WebSocket событий
// Обработчик WebSocket событий
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("🔌 [%u] Отключен!\n", num);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("✅ [%u] Подключен от %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        
        // Отправка приветственного сообщения
        //String welcome = "{\"type\":\"status\",\"message\":\"Подключен к MPU6050 трекеру\"}";
        //webSocket.sendTXT(num, welcome);
        
        // Отправка статуса калибровки
        String calStatus = "{\"type\":\"calibrationStatus\",\"calibrated\":" + String(calibrated ? "true" : "false") + "}";
        webSocket.sendTXT(num, calStatus);
        
        // Отправка статуса авто-калибровки
        String autoCalStatus = "{\"type\":\"autoCalibrationStatus\",\"enabled\":" + String(autoCalibrationEnabled ? "true" : "false") + "}";
        webSocket.sendTXT(num, autoCalStatus);
        
        if (zeroSet) {
          String zeroInfo = "{\"type\":\"zeroInfo\",\"zeroPitch\":" + String(zeroPitch, 2) + 
                           ",\"zeroRoll\":" + String(zeroRoll, 2) + 
                           ",\"zeroYaw\":" + String(zeroYaw, 2) + "}";
          webSocket.sendTXT(num, zeroInfo);
        }
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("📨 [%u] Получено: %s\n", num, payload);
        
        DynamicJsonDocument doc(256);
        deserializeJson(doc, message);
        String command = doc["type"];
        
        if (command == "ledOn") {
          ledState = HIGH;
          digitalWrite(LED_BUILTIN, ledState);
          String response = "{\"type\":\"status\",\"message\":\"LED включен\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "ledOff") {
          ledState = LOW;
          digitalWrite(LED_BUILTIN, ledState);
          String response = "{\"type\":\"status\",\"message\":\"LED выключен\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "blink") {
          String response = "{\"type\":\"status\",\"message\":\"LED мигает\"}";
          webSocket.sendTXT(num, response);
          for(int i = 0; i < 10; i++) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(500);
          }
          digitalWrite(LED_BUILTIN, ledState);
        }
        else if (command == "setZero") {
          setZeroPoint();
          String response = "{\"type\":\"status\",\"message\":\"Нулевая точка установлена\"}";
          webSocket.sendTXT(num, response);
          String zeroInfo = "{\"type\":\"zeroInfo\",\"zeroPitch\":" + String(zeroPitch, 2) + 
                           ",\"zeroRoll\":" + String(zeroRoll, 2) + 
                           ",\"zeroYaw\":" + String(zeroYaw, 2) + "}";
          webSocket.broadcastTXT(zeroInfo);
        }
        else if (command == "resetZero") {
          resetZeroPoint();
          String response = "{\"type\":\"status\",\"message\":\"Нулевая точка сброшена\"}";
          webSocket.sendTXT(num, response);
          String zeroReset = "{\"type\":\"zeroReset\"}";
          webSocket.broadcastTXT(zeroReset);
        }
        else if (command == "resetYaw") {
          resetYaw();
          String response = "{\"type\":\"status\",\"message\":\"Yaw сброшен\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "recalibrate") {
          recalibrate();
          String response = "{\"type\":\"status\",\"message\":\"Перекалибровка запущена\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "setAutoCalibration") {
          bool enable = doc["enable"];
          setAutoCalibration(enable);
          String response = "{\"type\":\"status\",\"message\":\"Авто-калибровка " + String(enable ? "включена" : "выключена") + "\"}";
          webSocket.sendTXT(num, response);
          String autoCalUpdate = "{\"type\":\"autoCalibrationUpdate\",\"enabled\":" + String(enable ? "true" : "false") + "}";
          webSocket.broadcastTXT(autoCalUpdate);
        }
        else if (command == "restart") {
          String response = "{\"type\":\"status\",\"message\":\"Перезагрузка...\"}";
          webSocket.sendTXT(num, response);
          delay(1000);
          ESP.restart();
        // В функции webSocketEvent в блоке WStype_TEXT добавить:
        } else if (command == "resetAngles") {
          resetAllAngles();
          String response = "{\"type\":\"status\",\"message\":\"Все углы сброшены\"}";
          webSocket.sendTXT(num, response);
        }          
      }
      break;
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
  info += "Размер Flash: " + String(ESP.getFlashChipSize()) + " байт\n\n";
  
  info += "=== MPU6050 ===\n";
  info += "Подключен: " + String(mpuConnected ? "Да" : "Нет") + "\n";
  info += "Калиброван: " + String(calibrated ? "Да" : "Нет") + "\n";
  info += "Авто-калибровка: " + String(autoCalibrationEnabled ? "Включена" : "Выключена") + "\n";
  info += "Нулевая точка: " + String(zeroSet ? "Установлена" : "Не установлена") + "\n";
  info += "Pitch: " + String(smoothedPitch, 2) + "°\n";
  info += "Roll: " + String(smoothedRoll, 2) + "°\n";
  info += "Yaw: " + String(smoothedYaw, 2) + "°\n";
  
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
  
  // Инициализация MPU6050
  mpuConnected = initializeMPU6050();
  if (mpuConnected) {
    calibrationStart = millis();
    Serial.println("🔧 Калибровка гироскопа... Держите сенсор неподвижно 3 секунды!");
  }
  
  // Настройка веб-сервера
  server.on("/", handleRoot);
  server.on("/info", handleInfo);
  server.on("/wifi-scan", handleWiFiScan);
  server.on("/restart", handleRestart);
  server.onNotFound(handleNotFound);
  
  // Запуск серверов
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  Serial.println("HTTP сервер запущен на порту 80");
  Serial.println("WebSocket сервер запущен на порту 81");
  
  // Запоминаем время старта
  startTime = millis();
  
  Serial.println("Готово! Откройте в браузере ваш IP адрес:");
  Serial.println(WiFi.localIP());
  Serial.println("WebSocket: ws://" + WiFi.localIP().toString() + ":81");
}

void loop() {
  // Обработка HTTP клиентов
  server.handleClient();
  
  // Обработка WebSocket клиентов
  webSocket.loop();
  
  // Обработка данных сенсора
  if (mpuConnected) {
    processSensorData();
  }
  
  // Отправка данных сенсора
  unsigned long currentTime = millis();
  if (currentTime - lastDataSend >= DATA_SEND_INTERVAL) {
    if (mpuConnected && calibrated) {
      sendSensorData();
    }
    lastDataSend = currentTime;
  }
  
  delay(10);
}
