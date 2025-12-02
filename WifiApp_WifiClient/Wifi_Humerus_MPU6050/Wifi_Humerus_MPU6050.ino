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

// Данные сенсора для плечевой кости
float pitch = 0, roll = 0, yaw = 0;
float smoothedPitch = 0, smoothedRoll = 0, smoothedYaw = 0;
const float smoothingFactor = 0.3;

// Фильтр и калибровка
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
bool calibrated = false;
unsigned long lastTime = 0;
unsigned long calibrationStart = 0;
const unsigned long calibrationTime = 3000;

// Нулевая точка для плечевой кости
float zeroPitch = 0, zeroRoll = 0, zeroYaw = 0;
bool zeroSet = false;

// Авто-калибровка
bool autoCalibrationEnabled = true;
const unsigned long AUTO_CALIBRATION_INTERVAL = 60000;
unsigned long lastAutoCalibration = 0;

// Управление отправкой данных
unsigned long lastDataSend = 0;
const unsigned long DATA_SEND_INTERVAL = 50;

// Режим работы для плечевой кости
enum ArmMode {
  ARM_MODE_RELATIVE,    // Относительные углы
  ARM_MODE_ABSOLUTE     // Абсолютные углы
};
ArmMode currentArmMode = ARM_MODE_RELATIVE;

// HTML страница с специализированным интерфейсом для плечевой кости
const char* htmlPage = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Датчик плечевой кости - MPU6050</title>
  <style>
    body { 
      font-family: Arial, sans-serif; 
      margin: 0; 
      padding: 20px; 
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      overflow-x: hidden;
      color: white;
    }
    .container { 
      max-width: 1000px; 
      margin: 0 auto; 
      background: rgba(255,255,255,0.1); 
      padding: 25px; 
      border-radius: 20px; 
      backdrop-filter: blur(10px);
      box-shadow: 0 8px 32px rgba(0,0,0,0.3);
    }
    h1 { 
      text-align: center; 
      margin-bottom: 10px;
      text-shadow: 2px 2px 4px rgba(0,0,0,0.5);
    }
    .subtitle {
      text-align: center;
      margin-bottom: 30px;
      opacity: 0.9;
    }
    .dashboard {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 20px;
      margin: 20px 0;
    }
    @media (max-width: 768px) {
      .dashboard {
        grid-template-columns: 1fr;
      }
    }
    .sensor-panel {
      background: rgba(255,255,255,0.15);
      padding: 20px;
      border-radius: 15px;
      border: 1px solid rgba(255,255,255,0.2);
    }
    .angle-display {
      font-size: 2.5rem;
      font-weight: bold;
      text-align: center;
      margin: 15px 0;
      text-shadow: 2px 2px 4px rgba(0,0,0,0.5);
    }
    .angle-label {
      font-size: 1rem;
      opacity: 0.8;
      text-align: center;
    }
    .angles-grid {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 15px;
      margin: 20px 0;
    }
    .angle-card {
      background: rgba(0,0,0,0.3);
      padding: 15px;
      border-radius: 10px;
      text-align: center;
    }
    .control-panel {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 12px;
      margin: 25px 0;
    }
    .button { 
      background: rgba(255,255,255,0.2); 
      color: white; 
      padding: 12px 15px; 
      border: none; 
      border-radius: 8px; 
      cursor: pointer; 
      font-size: 14px;
      margin: 3px;
      transition: all 0.3s ease;
      border: 1px solid rgba(255,255,255,0.3);
    }
    .button:hover { 
      background: rgba(255,255,255,0.3);
      transform: translateY(-2px);
    }
    .button-primary { 
      background: #4CAF50;
      border-color: #4CAF50;
    }
    .button-primary:hover { 
      background: #45a049;
    }
    .button-warning { 
      background: #ff9800;
      border-color: #ff9800;
    }
    .button-warning:hover { 
      background: #f57c00;
    }
    .button-danger { 
      background: #f44336;
      border-color: #f44336;
    }
    .button-danger:hover { 
      background: #da190b;
    }
    .status-indicators {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 15px;
      margin: 20px 0;
    }
    .status-item {
      background: rgba(255,255,255,0.1);
      padding: 15px;
      border-radius: 10px;
      text-align: center;
    }
    .status-value {
      font-weight: bold;
      margin-top: 5px;
    }
    .connected { color: #4CAF50; }
    .disconnected { color: #f44336; }
    .calibrating { color: #ff9800; }
    
    /* Стили для визуализации плечевой кости */
    #armVisualization {
      width: 100%;
      height: 300px;
      background: rgba(0,0,0,0.3);
      border-radius: 15px;
      margin: 20px 0;
      position: relative;
      overflow: hidden;
    }
    #armCanvas {
      width: 100%;
      height: 100%;
    }
    
    .mode-selector {
      display: flex;
      justify-content: center;
      gap: 10px;
      margin: 15px 0;
    }
    .mode-btn {
      padding: 8px 16px;
      border: 2px solid rgba(255,255,255,0.3);
      background: transparent;
      color: white;
      border-radius: 20px;
      cursor: pointer;
      transition: all 0.3s ease;
    }
    .mode-btn.active {
      background: rgba(255,255,255,0.2);
      border-color: #4CAF50;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>🦴 Датчик плечевой кости</h1>
    <div class="subtitle">MPU6050 | Отслеживание положения руки в реальном времени</div>
    
    <div class="status-indicators">
      <div class="status-item">
        <div>Статус подключения</div>
        <div class="status-value" id="connectionStatus">Отключен</div>
      </div>
      <div class="status-item">
        <div>Калибровка</div>
        <div class="status-value" id="calibrationStatus">Не калиброван</div>
      </div>
      <div class="status-item">
        <div>Нулевая точка</div>
        <div class="status-value" id="zeroStatus">Не установлена</div>
      </div>
      <div class="status-item">
        <div>Режим работы</div>
        <div class="status-value" id="armMode">Относительный</div>
      </div>
    </div>

    <div class="dashboard">
      <div class="sensor-panel">
        <h3>📊 Углы ориентации</h3>
        <div class="angles-grid">
          <div class="angle-card">
            <div class="angle-label">Pitch (Наклон)</div>
            <div class="angle-display" id="pitchValue">0.0°</div>
            <div>Вперед/назад</div>
          </div>
          <div class="angle-card">
            <div class="angle-label">Roll (Крен)</div>
            <div class="angle-display" id="rollValue">0.0°</div>
            <div>Вращение вокруг оси</div>
          </div>
          <div class="angle-card">
            <div class="angle-label">Yaw (Рыскание)</div>
            <div class="angle-display" id="yawValue">0.0°</div>
            <div>Влево/вправо</div>
          </div>
        </div>
        
        <div class="mode-selector">
          <button class="mode-btn active" onclick="setArmMode('relative')">Относительные углы</button>
          <button class="mode-btn" onclick="setArmMode('absolute')">Абсолютные углы</button>
        </div>
      </div>

      <div class="sensor-panel">
        <h3>🎯 Визуализация плечевой кости</h3>
        <div id="armVisualization">
          <canvas id="armCanvas"></canvas>
        </div>
        <div style="text-align: center; margin-top: 10px; opacity: 0.8;">
          Модель плечевой кости в реальном времени
        </div>
      </div>
    </div>

    <div class="control-panel">
      <div>
        <h4>Калибровка</h4>
        <button class="button button-primary" onclick="sendCommand('recalibrate')">⚙️ Калибровать</button>
        <button class="button button-warning" onclick="sendCommand('setZero')">🎯 Установить ноль</button>
        <button class="button" onclick="sendCommand('resetZero')">🔄 Сбросить ноль</button>
      </div>
      
      <div>
        <h4>Управление углами</h4>
        <button class="button" onclick="sendCommand('resetYaw')">↩️ Сбросить Yaw</button>
        <button class="button" onclick="sendCommand('resetAngles')">🔄 Сбросить все углы</button>
      </div>
      
      <div>
        <h4>Настройки</h4>
        <button class="button" id="autoCalBtn" onclick="toggleAutoCalibration()">🔴 Выкл авто-калибровку</button>
        <button class="button" onclick="sendCommand('restart')">🔄 Перезагрузить</button>
      </div>
      
      <div>
        <h4>Диагностика</h4>
        <button class="button" onclick="location.href='/info'">ℹ️ Информация</button>
        <button class="button" onclick="testConnection()">📡 Тест связи</button>
      </div>
    </div>

    <div style="margin-top: 30px; text-align: center; opacity: 0.7; font-size: 14px;">
      <p>Датчик плечевой кости MPU6050 | Версия 2.0 | Для систем отслеживания движений</p>
    </div>
  </div>

  <script>
    let ws = null;
    let armCanvas, armCtx;
    let sensorData = { 
      pitch: 0, roll: 0, yaw: 0, 
      relPitch: 0, relRoll: 0, relYaw: 0,
      calibrated: false, zeroSet: false 
    };
    let currentMode = 'relative';

    function connectWebSocket() {
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      const wsUrl = `${protocol}//${window.location.hostname}:81`;
      
      ws = new WebSocket(wsUrl);
      
      ws.onopen = function() {
        document.getElementById('connectionStatus').textContent = 'Подключено';
        document.getElementById('connectionStatus').className = 'status-value connected';
        console.log('WebSocket connected');
      };
      
      ws.onclose = function() {
        document.getElementById('connectionStatus').textContent = 'Отключен';
        document.getElementById('connectionStatus').className = 'status-value disconnected';
        console.log('WebSocket disconnected');
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
        sensorData = data;
        updateSensorDisplay();
        drawArmVisualization();
      } else if (data.type === 'status') {
        showNotification(data.message);
      } else if (data.type === 'calibrationStatus') {
        const statusEl = document.getElementById('calibrationStatus');
        statusEl.textContent = data.calibrated ? 'Готов' : 'Калибруется...';
        statusEl.className = data.calibrated ? 'status-value connected' : 'status-value calibrating';
      } else if (data.type === 'autoCalibrationStatus') {
        const btn = document.getElementById('autoCalBtn');
        btn.textContent = data.enabled ? '🔴 Выкл авто-калибровку' : '🟢 Вкл авто-калибровку';
      } else if (data.type === 'zeroInfo') {
        document.getElementById('zeroStatus').textContent = 'Установлена';
      } else if (data.type === 'zeroReset') {
        document.getElementById('zeroStatus').textContent = 'Не установлена';
      } else if (data.type === 'armMode') {
        currentMode = data.mode;
        document.getElementById('armMode').textContent = data.mode === 'relative' ? 'Относительный' : 'Абсолютный';
        updateModeButtons();
      }
    }

    function updateSensorDisplay() {
      const pitch = currentMode === 'relative' ? sensorData.relPitch : sensorData.pitch;
      const roll = currentMode === 'relative' ? sensorData.relRoll : sensorData.roll;
      const yaw = currentMode === 'relative' ? sensorData.relYaw : sensorData.yaw;
      
      document.getElementById('pitchValue').textContent = pitch.toFixed(1) + '°';
      document.getElementById('rollValue').textContent = roll.toFixed(1) + '°';
      document.getElementById('yawValue').textContent = yaw.toFixed(1) + '°';
    }

    function setArmMode(mode) {
      currentMode = mode;
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'setArmMode', mode: mode }));
      }
      updateModeButtons();
      updateSensorDisplay();
    }

    function updateModeButtons() {
      document.querySelectorAll('.mode-btn').forEach(btn => {
        btn.classList.remove('active');
      });
      document.querySelector(`.mode-btn[onclick="setArmMode('${currentMode}')"]`).classList.add('active');
      document.getElementById('armMode').textContent = currentMode === 'relative' ? 'Относительный' : 'Абсолютный';
    }

    function sendCommand(command) {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: command }));
      } else {
        alert('WebSocket не подключен!');
      }
    }

    function toggleAutoCalibration() {
      if (ws && ws.readyState === WebSocket.OPEN) {
        const btn = document.getElementById('autoCalBtn');
        const currentlyEnabled = btn.textContent.includes('Выкл');
        ws.send(JSON.stringify({ 
          type: 'setAutoCalibration', 
          enable: !currentlyEnabled 
        }));
      }
    }

    function testConnection() {
      if (ws && ws.readyState === WebSocket.OPEN) {
        showNotification('Соединение активно');
      } else {
        showNotification('Нет соединения');
      }
    }

    function showNotification(message) {
      // Простое уведомление
      alert(message);
    }

    // Визуализация плечевой кости
    function initArmVisualization() {
      armCanvas = document.getElementById('armCanvas');
      armCtx = armCanvas.getContext('2d');
      
      function resizeCanvas() {
        const container = document.getElementById('armVisualization');
        armCanvas.width = container.clientWidth;
        armCanvas.height = container.clientHeight;
      }
      
      window.addEventListener('resize', resizeCanvas);
      resizeCanvas();
    }

    function drawArmVisualization() {
      if (!armCtx) return;
      
      const width = armCanvas.width;
      const height = armCanvas.height;
      const centerX = width / 2;
      const centerY = height / 2;
      
      // Очистка canvas
      armCtx.fillStyle = 'rgba(0, 0, 0, 0.3)';
      armCtx.fillRect(0, 0, width, height);
      
      // Используем относительные углы для визуализации
      const pitch = currentMode === 'relative' ? sensorData.relPitch : sensorData.pitch;
      const roll = currentMode === 'relative' ? sensorData.relRoll : sensorData.roll;
      const yaw = currentMode === 'relative' ? sensorData.relYaw : sensorData.yaw;
      
      // Рисуем плечевую кость
      drawBone(centerX, centerY, pitch, roll, yaw);
      
      // Рисуем информационную панель
      drawInfoPanel();
    }

    function drawBone(x, y, pitch, roll, yaw) {
      const length = Math.min(armCanvas.width, armCanvas.height) * 0.3;
      
      // Преобразуем углы в радианы
      const pitchRad = (pitch * Math.PI) / 180;
      const rollRad = (roll * Math.PI) / 180;
      const yawRad = (yaw * Math.PI) / 180;
      
      // Вычисляем конечную точку кости с учетом углов
      const endX = x + length * Math.sin(yawRad) * Math.cos(pitchRad);
      const endY = y - length * Math.sin(pitchRad) * Math.cos(rollRad);
      
      // Рисуем кость
      armCtx.strokeStyle = '#9C27B0';
      armCtx.lineWidth = 15;
      armCtx.lineCap = 'round';
      armCtx.beginPath();
      armCtx.moveTo(x, y);
      armCtx.lineTo(endX, endY);
      armCtx.stroke();
      
      // Рисуем суставы
      drawJoint(x, y, '#4CAF50'); // Плечевой сустав
      drawJoint(endX, endY, '#FF9800'); // Локтевой сустав
      
      // Рисуем направляющие линии
      drawGuidelines(x, y);
    }

    function drawJoint(x, y, color) {
      armCtx.fillStyle = color;
      armCtx.beginPath();
      armCtx.arc(x, y, 8, 0, Math.PI * 2);
      armCtx.fill();
      
      armCtx.strokeStyle = 'white';
      armCtx.lineWidth = 2;
      armCtx.stroke();
    }

    function drawGuidelines(x, y) {
      const size = 50;
      
      // Ось X (красная)
      armCtx.strokeStyle = '#ff4444';
      armCtx.lineWidth = 2;
      armCtx.beginPath();
      armCtx.moveTo(x, y);
      armCtx.lineTo(x + size, y);
      armCtx.stroke();
      
      // Ось Y (зеленая)
      armCtx.strokeStyle = '#44ff44';
      armCtx.beginPath();
      armCtx.moveTo(x, y);
      armCtx.lineTo(x, y - size);
      armCtx.stroke();
      
      // Подписи осей
      armCtx.fillStyle = 'white';
      armCtx.font = '12px Arial';
      armCtx.fillText('X', x + size + 5, y);
      armCtx.fillText('Y', x, y - size - 5);
    }

    function drawInfoPanel() {
      armCtx.fillStyle = 'rgba(255, 255, 255, 0.9)';
      armCtx.font = '12px Arial';
      armCtx.textAlign = 'left';
      
      const info = [
        `Режим: ${currentMode === 'relative' ? 'Относительный' : 'Абсолютный'}`,
        `Pitch: ${sensorData.pitch.toFixed(1)}°`,
        `Roll: ${sensorData.roll.toFixed(1)}°`, 
        `Yaw: ${sensorData.yaw.toFixed(1)}°`
      ];
      
      info.forEach((text, index) => {
        armCtx.fillText(text, 10, 20 + index * 15);
      });
    }

    // Инициализация при загрузке страницы
    document.addEventListener('DOMContentLoaded', function() {
      initArmVisualization();
      connectWebSocket();
      setInterval(drawArmVisualization, 50);
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

// Инициализация MPU6050
bool initializeMPU6050() {
  Serial.println("🔍 Инициализация MPU6050 для плечевой кости...");
  
  if (mpu.begin()) {
    // Настройка MPU6050 для отслеживания движений руки
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG); // Увеличиваем диапазон для быстрых движений
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); // Увеличиваем полосу для более отзывчивого отслеживания
    
    Serial.println("✅ MPU6050 найден и инициализирован для плечевой кости");
    return true;
  } else {
    Serial.println("❌ MPU6050 не найден!");
    return false;
  }
}

// Обработка данных сенсора для плечевой кости
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
  
  // Интеграция гироскопа для получения углов
  pitch += gyroX * deltaTime * 180.0 / PI;
  roll += gyroY * deltaTime * 180.0 / PI;
  yaw += gyroZ * deltaTime * 180.0 / PI;
  
  // Сглаживание для отображения
  smoothedPitch = smoothedPitch * (1 - smoothingFactor) + pitch * smoothingFactor;
  smoothedRoll = smoothedRoll * (1 - smoothingFactor) + roll * smoothingFactor;
  smoothedYaw = smoothedYaw * (1 - smoothingFactor) + yaw * smoothingFactor;
  
  // Отладочный вывод для отслеживания движений руки
  static unsigned long lastDebug = 0;
  if (currentTime - lastDebug > 5000) { // Каждые 5 секунд
    lastDebug = currentTime;
    Serial.printf("🦴 Плечевая кость - Pitch: %.1f°, Roll: %.1f°, Yaw: %.1f°\n", 
                 smoothedPitch, smoothedRoll, smoothedYaw);
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
      Serial.printf("Калибровка плечевого датчика: %d%%\n", progress);
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
    
    Serial.println("✅ Калибровка гироскопа для плечевой кости завершена!");
    Serial.printf("Обработано samples: %d\n", sampleCount);
    
    // Уведомление клиентов
    String statusMsg = "{\"type\":\"status\",\"message\":\"Калибровка плечевого датчика завершена\"}";
    webSocket.broadcastTXT(statusMsg);
    String calStatus = "{\"type\":\"calibrationStatus\",\"calibrated\":true}";
    webSocket.broadcastTXT(calStatus);
  }
}

// Отправка данных сенсора через WebSocket
void sendSensorData() {
  if (webSocket.connectedClients() == 0) return;
  
  // Расчет относительных углов
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
  
  Serial.printf("💾 Нулевая точка установлена для плеча - Pitch:%.1f° Roll:%.1f° Yaw:%.1f°\n", 
               zeroPitch, zeroRoll, zeroYaw);
               
  String zeroInfo = "{\"type\":\"zeroInfo\"}";
  webSocket.broadcastTXT(zeroInfo);
}

// Сброс нулевой точки
void resetZeroPoint() {
  zeroPitch = 0;
  zeroRoll = 0;
  zeroYaw = 0;
  zeroSet = false;
  
  Serial.println("🔄 Нулевая точка сброшена для плеча");
  
  String zeroReset = "{\"type\":\"zeroReset\"}";
  webSocket.broadcastTXT(zeroReset);
}

// Сброс Yaw
void resetYaw() {
  yaw = 0;
  smoothedYaw = 0;
  
  Serial.println("🔄 Yaw сброшен для плеча");
}

// Сброс всех углов
void resetAllAngles() {
  pitch = 0;
  roll = 0;
  yaw = 0;
  smoothedPitch = 0;
  smoothedRoll = 0;
  smoothedYaw = 0;
  Serial.println("🔄 Все углы сброшены для плеча");
}

// Перекалибровка
void recalibrate() {
  calibrated = false;
  pitch = roll = yaw = 0;
  calibrationStart = millis();
  
  Serial.println("🔄 Перекалибровка плечевого датчика запущена");
  
  String calStatus = "{\"type\":\"calibrationStatus\",\"calibrated\":false}";
  webSocket.broadcastTXT(calStatus);
}

// Установка авто-калибровки
void setAutoCalibration(bool enable) {
  autoCalibrationEnabled = enable;
  if (enable) {
    lastAutoCalibration = millis();
  }
  
  Serial.printf("⚙️ Авто-калибровка плечевого датчика %s\n", enable ? "включена" : "выключена");
  
  String autoCalStatus = "{\"type\":\"autoCalibrationStatus\",\"enabled\":" + String(enable ? "true" : "false") + "}";
  webSocket.broadcastTXT(autoCalStatus);
}

// Установка режима работы
void setArmMode(String mode) {
  if (mode == "relative") {
    currentArmMode = ARM_MODE_RELATIVE;
  } else {
    currentArmMode = ARM_MODE_ABSOLUTE;
  }
  
  Serial.printf("🎯 Режим плечевого датчика установлен: %s\n", mode.c_str());
  
  String modeMsg = "{\"type\":\"armMode\",\"mode\":\"" + mode + "\"}";
  webSocket.broadcastTXT(modeMsg);
}

// Обработчик WebSocket событий
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("🔌 [%u] Отключен от плечевого датчика!\n", num);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("✅ [%u] Подключен к плечевому датчику от %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        
        // Отправка текущего статуса
        String calStatus = "{\"type\":\"calibrationStatus\",\"calibrated\":" + String(calibrated ? "true" : "false") + "}";
        webSocket.sendTXT(num, calStatus);
        
        String autoCalStatus = "{\"type\":\"autoCalibrationStatus\",\"enabled\":" + String(autoCalibrationEnabled ? "true" : "false") + "}";
        webSocket.sendTXT(num, autoCalStatus);
        
        String modeMsg = "{\"type\":\"armMode\",\"mode\":\"" + String(currentArmMode == ARM_MODE_RELATIVE ? "relative" : "absolute") + "\"}";
        webSocket.sendTXT(num, modeMsg);
        
        if (zeroSet) {
          String zeroInfo = "{\"type\":\"zeroInfo\"}";
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
        
        if (command == "setZero") {
          setZeroPoint();
          String response = "{\"type\":\"status\",\"message\":\"Нулевая точка установлена для плеча\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "resetZero") {
          resetZeroPoint();
          String response = "{\"type\":\"status\",\"message\":\"Нулевая точка сброшена для плеча\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "resetYaw") {
          resetYaw();
          String response = "{\"type\":\"status\",\"message\":\"Yaw сброшен для плеча\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "resetAngles") {
          resetAllAngles();
          String response = "{\"type\":\"status\",\"message\":\"Все углы сброшены для плеча\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "recalibrate") {
          recalibrate();
          String response = "{\"type\":\"status\",\"message\":\"Перекалибровка плечевого датчика запущена\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "setAutoCalibration") {
          bool enable = doc["enable"];
          setAutoCalibration(enable);
          String response = "{\"type\":\"status\",\"message\":\"Авто-калибровка " + String(enable ? "включена" : "выключена") + "\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "setArmMode") {
          String mode = doc["mode"];
          setArmMode(mode);
          String response = "{\"type\":\"status\",\"message\":\"Режим установлен: " + mode + "\"}";
          webSocket.sendTXT(num, response);
        }
        else if (command == "restart") {
          String response = "{\"type\":\"status\",\"message\":\"Перезагрузка плечевого датчика...\"}";
          webSocket.sendTXT(num, response);
          delay(1000);
          ESP.restart();
        }
      }
      break;
  }
}

// Обработчик главной страницы
void handleRoot() {
  visitorCount++;
  
  String page = htmlPage;
  page.replace("%UPTIME%", formatTime(millis() - startTime));
  page.replace("%VISITORS%", String(visitorCount));
  page.replace("%IPADDRESS%", WiFi.localIP().toString());
  page.replace("%SSID%", WiFi.SSID());
  page.replace("%RSSI%", String(WiFi.RSSI()));
  page.replace("%CHIPID%", String(ESP.getChipId()));
  
  server.send(200, "text/html", page);
}

// Информация о системе
void handleInfo() {
  String info = "Информация о датчике плечевой кости:\n\n";
  info += "=== WiFi ===\n";
  info += "Статус: " + String(WiFi.status() == WL_CONNECTED ? "Подключено" : "Не подключено") + "\n";
  info += "SSID: " + WiFi.SSID() + "\n";
  info += "IP адрес: " + WiFi.localIP().toString() + "\n";
  info += "Сила сигнала: " + String(WiFi.RSSI()) + " dBm\n\n";
  
  info += "=== Система ===\n";
  info += "Время работы: " + formatTime(millis() - startTime) + "\n";
  info += "Посетителей: " + String(visitorCount) + "\n";
  info += "ID чипа: " + String(ESP.getChipId()) + "\n";
  info += "Свободная память: " + String(ESP.getFreeHeap()) + " байт\n\n";
  
  info += "=== Плечевой датчик ===\n";
  info += "Подключен: " + String(mpuConnected ? "Да" : "Нет") + "\n";
  info += "Калиброван: " + String(calibrated ? "Да" : "Нет") + "\n";
  info += "Авто-калибровка: " + String(autoCalibrationEnabled ? "Включена" : "Выключена") + "\n";
  info += "Нулевая точка: " + String(zeroSet ? "Установлена" : "Не установлена") + "\n";
  info += "Режим: " + String(currentArmMode == ARM_MODE_RELATIVE ? "Относительный" : "Абсолютный") + "\n";
  info += "Pitch: " + String(smoothedPitch, 2) + "°\n";
  info += "Roll: " + String(smoothedRoll, 2) + "°\n";
  info += "Yaw: " + String(smoothedYaw, 2) + "°\n";
  
  server.send(200, "text/plain", info);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Настройка встроенного LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  ledState = LOW;
  
  // Подключение к WiFi
  Serial.println();
  Serial.println("🦴 Подключение датчика плечевой кости к WiFi...");
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
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi подключен!");
    Serial.print("📡 IP адрес: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    Serial.println("❌ Не удалось подключиться к WiFi!");
    WiFi.softAP("Shoulder_Sensor", "12345678");
    Serial.print("🔄 Запущен резервный AP. IP: ");
    Serial.println(WiFi.softAPIP());
  }
  
  // Инициализация MPU6050
  mpuConnected = initializeMPU6050();
  if (mpuConnected) {
    calibrationStart = millis();
    Serial.println("🔧 Калибровка гироскопа плечевого датчика... Держите руку неподвижно 3 секунды!");
  }
  
  // Настройка веб-сервера
  server.on("/", handleRoot);
  server.on("/info", handleInfo);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Страница не найдена\nДоступные страницы:\n/ - Интерфейс плечевого датчика\n/info - Информация о системе");
  });
  
  // Запуск серверов
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  Serial.println("✅ HTTP сервер запущен на порту 80");
  Serial.println("✅ WebSocket сервер запущен на порту 81");
  
  // Запоминаем время старта
  startTime = millis();
  
  Serial.println("🎯 Готово! Датчик плечевой кости активен.");
  Serial.println("🌐 Откройте в браузере: http://" + WiFi.localIP().toString());
  Serial.println("📡 WebSocket: ws://" + WiFi.localIP().toString() + ":81");
}

void loop() {
  server.handleClient();
  webSocket.loop();
  
  if (mpuConnected) {
    processSensorData();
  }
  
  unsigned long currentTime = millis();
  if (currentTime - lastDataSend >= DATA_SEND_INTERVAL) {
    if (mpuConnected && calibrated) {
      sendSensorData();
    }
    lastDataSend = currentTime;
  }
  
  // Авто-калибровка каждые 60 секунд
  if (autoCalibrationEnabled && currentTime - lastAutoCalibration >= AUTO_CALIBRATION_INTERVAL) {
    lastAutoCalibration = currentTime;
    if (mpuConnected && calibrated) {
      Serial.println("🔄 Автоматическая калибровка плечевого датчика...");
      recalibrate();
    }
  }
  
  delay(10);
}
