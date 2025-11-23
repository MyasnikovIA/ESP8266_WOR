/*
  MPU6050 Head Tracker with Gaze Direction
  Enhanced for head-mounted sensor with gaze direction calculation
  Stable real-time orientation data transmission
  Fixed yaw drift issue
  Dual mode: Serial + WebSocket or WebSocket only
  Added 3D visualization, zero point saving, and absolute/relative positioning
  Added head tilt logic from MPU6050_004
  Added continuous angle accumulation with zero-crossing detection
  Added idle yaw increment feature
  Added gaze direction calculation based on head orientation
  Integrated web interface
*/

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <EEPROM.h>


const char* ssid = "ESP8266_AP";
const char* password = "12345678";

// HTML Parts - объявляем в начале файла
const char HTML_HEAD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>MPU6050 VR Tracker</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 0;
            padding: 20px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            color: #333;
        }
        .container { 
            max-width: 1200px; 
            margin: 0 auto; 
            background: rgba(255,255,255,0.95); 
            padding: 20px; 
            border-radius: 15px; 
            box-shadow: 0 8px 32px rgba(0,0,0,0.1);
        }
        .header { 
            text-align: center; 
            margin-bottom: 30px;
            background: linear-gradient(135deg, #4CAF50, #45a049);
            color: white;
            padding: 20px;
            border-radius: 10px;
        }
        .dashboard {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
            margin-bottom: 20px;
        }
        .panel {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 10px;
            border-left: 4px solid #4CAF50;
        }
        .visualization-panel {
            grid-column: 1 / -1;
            background: #2c3e50;
            color: white;
            padding: 20px;
            border-radius: 10px;
        }
        .controls {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 10px;
            margin: 20px 0;
        }
        .btn {
            padding: 12px 15px;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-size: 14px;
            font-weight: bold;
            transition: all 0.3s;
        }
        .btn-primary { background: #4CAF50; color: white; }
        .btn-warning { background: #ff9800; color: white; }
        .btn-danger { background: #f44336; color: white; }
        .btn-info { background: #2196F3; color: white; }
        .btn-secondary { background: #6c757d; color: white; }
        .btn:hover { transform: translateY(-2px); box-shadow: 0 4px 8px rgba(0,0,0,0.2); }
        
        .data-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin: 20px 0;
        }
        .data-card {
            background: white;
            padding: 15px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            text-align: center;
        }
        .data-value {
            font-size: 24px;
            font-weight: bold;
            margin: 10px 0;
        }
        .data-label {
            font-size: 12px;
            color: #666;
            text-transform: uppercase;
        }
        .direction-positive { color: #4CAF50; }
        .direction-negative { color: #f44336; }
        .direction-zero { color: #ff9800; }
        
        .cube-container {
            width: 300px;
            height: 300px;
            margin: 20px auto;
            perspective: 1000px;
        }
        .cube {
            width: 100%;
            height: 100%;
            position: relative;
            transform-style: preserve-3d;
            transition: transform 0.1s ease-out;
        }
        .face {
            position: absolute;
            width: 300px;
            height: 300px;
            border: 3px solid #34495e;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 24px;
            font-weight: bold;
            color: white;
            background: rgba(52, 152, 219, 0.8);
        }
        .front  { transform: rotateY(0deg) translateZ(150px); background: rgba(231, 76, 60, 0.8); }
        .back   { transform: rotateY(180deg) translateZ(150px); background: rgba(52, 152, 219, 0.8); }
        .right  { transform: rotateY(90deg) translateZ(150px); background: rgba(46, 204, 113, 0.8); }
        .left   { transform: rotateY(-90deg) translateZ(150px); background: rgba(155, 89, 182, 0.8); }
        .top    { transform: rotateX(90deg) translateZ(150px); background: rgba(241, 196, 15, 0.8); }
        .bottom { transform: rotateX(-90deg) translateZ(150px); background: rgba(230, 126, 34, 0.8); }
        
        .connection-status {
            position: fixed;
            top: 20px;
            right: 20px;
            padding: 10px 15px;
            border-radius: 20px;
            font-weight: bold;
            z-index: 1000;
        }
        .connected { background: #4CAF50; color: white; }
        .disconnected { background: #f44336; color: white; }
        .config { background: #ff9800; color: white; }
        
        .config-panel {
            background: #fff3cd;
            border: 1px solid #ffeaa7;
            border-radius: 8px;
            padding: 15px;
            margin: 15px 0;
        }
        .config-input {
            padding: 10px;
            border: 2px solid #ddd;
            border-radius: 5px;
            font-size: 16px;
            width: 200px;
            margin-right: 10px;
        }
        .config-label {
            font-weight: bold;
            margin-right: 10px;
            color: #856404;
        }
        
        .info-section {
            background: #e8f5e8;
            padding: 15px;
            border-radius: 8px;
            margin: 15px 0;
            border-left: 4px solid #4CAF50;
        }
        
        .notification {
            position: fixed;
            top: 80px;
            right: 20px;
            background: #4CAF50;
            color: white;
            padding: 15px 20px;
            border-radius: 5px;
            z-index: 1001;
            box-shadow: 0 4px 8px rgba(0,0,0,0.2);
            font-weight: bold;
            max-width: 300px;
        }
        
        @media (max-width: 768px) {
            .dashboard {
                grid-template-columns: 1fr;
            }
            .cube-container {
                width: 200px;
                height: 200px;
            }
            .face {
                width: 200px;
                height: 200px;
                font-size: 18px;
            }
            .front  { transform: rotateY(0deg) translateZ(100px); }
            .back   { transform: rotateY(180deg) translateZ(100px); }
            .right  { transform: rotateY(90deg) translateZ(100px); }
            .left   { transform: rotateY(-90deg) translateZ(100px); }
            .top    { transform: rotateX(90deg) translateZ(100px); }
            .bottom { transform: rotateX(-90deg) translateZ(100px); }
        }
    </style>
</head>
)rawliteral";

const char HTML_BODY_START[] PROGMEM = R"rawliteral(
<body>
    <div class="connection-status config" id="connectionStatus">
        ⚙️ Configure IP
    </div>

    <div class="container">
        <div class="header">
            <h1>🎮 MPU6050 VR Head Tracker</h1>
            <p>Real-time orientation tracking with 3D visualization</p>
        </div>
)rawliteral";

const char HTML_CONFIG_PANEL[] PROGMEM = R"rawliteral(
        <div class="config-panel" id="configPanel">
            <h3>🔧 Configuration</h3>
            <div style="margin: 15px 0;">
                <span class="config-label">Device IP Address:</span>
                <input type="text" class="config-input" id="ipInput" placeholder="192.168.31.110" value="192.168.31.110">
                <button class="btn btn-primary" onclick="saveConfig()">💾 Save & Connect</button>
                <button class="btn btn-secondary" onclick="loadDefaultIP()">🔄 Default IP</button>
            </div>
            <div style="font-size: 12px; color: #856404;">
                💡 Enter the IP address of your ESP8266 device with MPU6050 sensor
            </div>
        </div>
)rawliteral";

const char HTML_DASHBOARD_START[] PROGMEM = R"rawliteral(
        <div class="dashboard">
)rawliteral";

const char HTML_ABSOLUTE_PANEL[] PROGMEM = R"rawliteral(
            <div class="panel">
                <h3>📊 Absolute Orientation</h3>
                <div class="data-grid">
                    <div class="data-card">
                        <div class="data-label">Pitch (X)</div>
                        <div class="data-value" id="absPitch">0.00°</div>
                        <div class="data-label">Front-Back Tilt</div>
                    </div>
                    <div class="data-card">
                        <div class="data-label">Roll (Y)</div>
                        <div class="data-value" id="absRoll">0.00°</div>
                        <div class="data-label">Left-Right Tilt</div>
                    </div>
                    <div class="data-card">
                        <div class="data-label">Yaw (Z)</div>
                        <div class="data-value" id="absYaw">0.00°</div>
                        <div class="data-label">Head Rotation</div>
                    </div>
                </div>
            </div>
)rawliteral";

const char HTML_RELATIVE_PANEL[] PROGMEM = R"rawliteral(
            <div class="panel">
                <h3>🎯 Relative to Zero</h3>
                <div class="data-grid">
                    <div class="data-card">
                        <div class="data-label">Pitch</div>
                        <div class="data-value">
                            <span id="relPitch">0.00°</span> <span id="dirPitch" class="direction-zero">●</span>
                        </div>
                    </div>
                    <div class="data-card">
                        <div class="data-label">Roll</div>
                        <div class="data-value">
                            <span id="relRoll">0.00°</span> <span id="dirRoll" class="direction-zero">●</span>
                        </div>
                    </div>
                    <div class="data-card">
                        <div class="data-label">Yaw</div>
                        <div class="data-value">
                            <span id="relYaw">0.00°</span> <span id="dirYaw" class="direction-zero">●</span>
                        </div>
                    </div>
                </div>
            </div>
)rawliteral";

const char HTML_VISUALIZATION_PANEL[] PROGMEM = R"rawliteral(
            <div class="visualization-panel">
                <h3>🎮 3D Head Orientation</h3>
                <div class="cube-container">
                    <div class="cube" id="cube">
                        <div class="face front">FACE</div>
                        <div class="face back">BACK</div>
                        <div class="face right">RIGHT</div>
                        <div class="face left">LEFT</div>
                        <div class="face top">TOP</div>
                        <div class="face bottom">BOTTOM</div>
                    </div>
                </div>
                
                <div class="data-grid">
                    <div class="data-card">
                        <div class="data-label">Accumulated Pitch</div>
                        <div class="data-value" id="accPitch">0.00°</div>
                    </div>
                    <div class="data-card">
                        <div class="data-label">Accumulated Roll</div>
                        <div class="data-value" id="accRoll">0.00°</div>
                    </div>
                    <div class="data-card">
                        <div class="data-label">Accumulated Yaw</div>
                        <div class="data-value" id="accYaw">0.00°</div>
                    </div>
                </div>
            </div>
)rawliteral";

const char HTML_DASHBOARD_END[] PROGMEM = R"rawliteral(
        </div>
)rawliteral";

const char HTML_CONTROLS[] PROGMEM = R"rawliteral(
        <div class="controls">
            <button class="btn btn-primary" onclick="setZeroPoint()">
                🎯 Set Zero Point
            </button>
            <button class="btn btn-warning" onclick="resetZeroPoint()">
                🔄 Reset Zero
            </button>
            <button class="btn btn-info" onclick="recalibrate()">
                🔧 Recalibrate
            </button>
            <button class="btn btn-danger" onclick="resetYaw()">
                🎯 Reset Yaw
            </button>
            <button class="btn btn-info" onclick="resetAccumulated()">
                📊 Reset Accumulated
            </button>
            <button class="btn btn-secondary" onclick="showConfig()">
            ⚙️ Change IP
            </button>
        </div>
)rawliteral";

const char HTML_SYSTEM_STATUS[] PROGMEM = R"rawliteral(
        <div class="info-section">
            <h4>📈 System Status</h4>
            <div class="data-grid">
                <div class="data-card">
                    <div class="data-label">Device State</div>
                    <div class="data-value" id="deviceState">-</div>
                </div>
                <div class="data-card">
                    <div class="data-label">WiFi Signal</div>
                    <div class="data-value" id="wifiSignal">-</div>
                </div>
                <div class="data-card">
                    <div class="data-label">Zero Point</div>
                    <div class="data-value" id="zeroStatus">-</div>
                </div>
                <div class="data-card">
                    <div class="data-label">Idle Mode</div>
                    <div class="data-value" id="idleStatus">-</div>
                </div>
            </div>
        </div>
)rawliteral";

const char HTML_HOW_TO_USE[] PROGMEM = R"rawliteral(
        <div class="info-section">
            <h4>💡 How to Use</h4>
            <ul>
                <li><strong>Set Zero Point:</strong> Set current head position as reference</li>
                <li><strong>Recalibrate:</strong> Fix sensor drift by recalibrating gyro</li>
                <li><strong>Reset Yaw:</strong> Reset yaw rotation to zero</li>
                <li><strong>Direction Indicators:</strong> Green ● = positive, Red ● = negative, Orange ● = stationary</li>
                <li><strong>Idle Mode:</strong> Yaw automatically increases when device is stationary</li>
                <li><strong>Change IP:</strong> Use the configuration panel to set your device's IP address</li>
            </ul>
        </div>
)rawliteral";

const char HTML_CONNECTION_INFO[] PROGMEM = R"rawliteral(
        <div class="info-section">
            <h4>🌐 Connection Info</h4>
            <div class="data-grid">
                <div class="data-card">
                    <div class="data-label">Current IP</div>
                    <div class="data-value" id="currentIP">-</div>
                </div>
                <div class="data-card">
                    <div class="data-label">WebSocket Port</div>
                    <div class="data-value">81</div>
                </div>
                <div class="data-card">
                    <div class="data-label">HTTP Port</div>
                    <div class="data-value">80</div>
                </div>
                <div class="data-card">
                    <div class="data-label">Reconnect Attempts</div>
                    <div class="data-value" id="reconnectCount">0</div>
                </div>
            </div>
        </div>
)rawliteral";

const char HTML_BODY_END[] PROGMEM = R"rawliteral(
    </div>
)rawliteral";

const char HTML_SCRIPT[] PROGMEM = R"rawliteral(
    <script>
        let ws = null;
        let cube = document.getElementById('cube');
        let connectionStatus = document.getElementById('connectionStatus');
        let configPanel = document.getElementById('configPanel');
        let reconnectAttempts = 0;
        const maxReconnectAttempts = 10;
        let currentIP = '';
        
        // Orientation data history for smoothing
        let orientationHistory = {
            pitch: [],
            roll: [],
            yaw: []
        };
        
        // Load saved IP from localStorage
        function loadSavedIP() {
            const savedIP = localStorage.getItem('mpu6050_ip');
            if (savedIP) {
                document.getElementById('ipInput').value = savedIP;
                currentIP = savedIP;
                document.getElementById('currentIP').textContent = savedIP;
            } else {
                // Use default IP
                currentIP = '192.168.31.110';
                document.getElementById('currentIP').textContent = currentIP;
            }
        }
        
        function saveConfig() {
            const ipInput = document.getElementById('ipInput').value.trim();
            
            // Basic IP validation
            if (!ipInput) {
                showNotification('Please enter an IP address', 'error');
                return;
            }
            
            // Simple IP format validation
            const ipRegex = /^(\d{1,3}\.){3}\d{1,3}$/;
            if (!ipRegex.test(ipInput)) {
                showNotification('Please enter a valid IP address', 'error');
                return;
            }
            
            // Save to localStorage
            localStorage.setItem('mpu6050_ip', ipInput);
            currentIP = ipInput;
            document.getElementById('currentIP').textContent = currentIP;
            
            showNotification(`IP address saved: ${ipInput}`, 'success');
            
            // Close config panel and connect
            configPanel.style.display = 'none';
            connectWebSocket();
        }
        
        function loadDefaultIP() {
            document.getElementById('ipInput').value = '192.168.31.110';
            showNotification('Default IP loaded', 'info');
        }
        
        function showConfig() {
            configPanel.style.display = 'block';
            // Disconnect if connected
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.close();
            }
        }
        
        function connectWebSocket() {
            if (!currentIP) {
                showNotification('Please configure IP address first', 'error');
                configPanel.style.display = 'block';
                return;
            }
            
            const wsUrl = `ws://${currentIP}:81`;
            
            connectionStatus.textContent = '🟡 Connecting...';
            connectionStatus.className = 'connection-status config';
            
            try {
                ws = new WebSocket(wsUrl);
                
                ws.onopen = function() {
                    console.log(`✅ WebSocket connected to ${currentIP}`);
                    connectionStatus.textContent = `🟢 Connected to ${currentIP}`;
                    connectionStatus.className = 'connection-status connected';
                    reconnectAttempts = 0;
                    document.getElementById('reconnectCount').textContent = '0';
                    showNotification(`Connected to ${currentIP}`, 'success');
                };
                
                ws.onmessage = function(event) {
                    try {
                        const data = JSON.parse(event.data);
                        handleSensorData(data);
                    } catch (e) {
                        console.error('Error parsing sensor data:', e);
                    }
                };
                
                ws.onclose = function(event) {
                    console.log(`❌ WebSocket disconnected from ${currentIP}`);
                    connectionStatus.textContent = `🔴 Disconnected from ${currentIP}`;
                    connectionStatus.className = 'connection-status disconnected';
                    
                    if (reconnectAttempts < maxReconnectAttempts) {
                        reconnectAttempts++;
                        const delay = Math.min(1000 * reconnectAttempts, 10000);
                        console.log(`Reconnecting in ${delay}ms... (attempt ${reconnectAttempts})`);
                        document.getElementById('reconnectCount').textContent = reconnectAttempts;
                        
                        setTimeout(connectWebSocket, delay);
                    } else {
                        showNotification(`Failed to connect after ${maxReconnectAttempts} attempts`, 'error');
                    }
                };
                
                ws.onerror = function(error) {
                    console.error('WebSocket error:', error);
                    showNotification(`Connection error to ${currentIP}`, 'error');
                };
                
            } catch (error) {
                console.error('Failed to create WebSocket:', error);
                showNotification('Failed to create WebSocket connection', 'error');
            }
        }
        
        function handleSensorData(data) {
            if (data.type === 'sensorData') {
                updateDashboard(data);
                update3DVisualization(data);
                updateSystemStatus(data);
            } else if (data.type === 'status') {
                console.log('System message:', data.message);
                showNotification(data.message, 'info');
            } else if (data.type === 'zeroInfo') {
                console.log('Zero point updated:', data);
                showNotification('Zero point set successfully', 'success');
            } else if (data.type === 'zeroReset') {
                console.log('Zero point reset');
                showNotification('Zero point reset', 'info');
            } else if (data.type === 'pong') {
                // Ping-pong response, no need to show
                console.log('Pong received');
            }
        }
        
        function updateDashboard(data) {
            // Update absolute orientation
            document.getElementById('absPitch').textContent = data.absPitch.toFixed(1) + '°';
            document.getElementById('absRoll').textContent = data.absRoll.toFixed(1) + '°';
            document.getElementById('absYaw').textContent = data.absYaw.toFixed(1) + '°';
            
            // Update relative orientation
            document.getElementById('relPitch').textContent = data.pitch.toFixed(1) + '°';
            document.getElementById('relRoll').textContent = data.roll.toFixed(1) + '°';
            document.getElementById('relYaw').textContent = data.yaw.toFixed(1) + '°';
            
            // Update accumulated angles
            document.getElementById('accPitch').textContent = data.accPitch.toFixed(1) + '°';
            document.getElementById('accRoll').textContent = data.accRoll.toFixed(1) + '°';
            document.getElementById('accYaw').textContent = data.accYaw.toFixed(1) + '°';
            
            // Update direction indicators
            updateDirectionIndicator('dirPitch', data.dirPitch);
            updateDirectionIndicator('dirRoll', data.dirRoll);
            updateDirectionIndicator('dirYaw', data.dirYaw);
        }
        
        function updateDirectionIndicator(elementId, direction) {
            const element = document.getElementById(elementId);
            element.textContent = direction === 1 ? '↗' : direction === -1 ? '↙' : '●';
            element.className = direction === 1 ? 'direction-positive' : 
                              direction === -1 ? 'direction-negative' : 'direction-zero';
        }
        
        function update3DVisualization(data) {
            // Apply smooth rotation to the cube with corrected logic
            const smoothPitch = smoothValue('pitch', data.pitch);
            const smoothRoll = smoothValue('roll', data.roll);
            const smoothYaw = smoothValue('yaw', data.yaw);
            
            // Исправленная логика вращения:
            // - Pitch вращает вокруг X оси (наклон вперед/назад)
            // - Yaw вращает вокруг Y оси (поворот головы влево/вправо)  
            // - Roll вращает вокруг Z оси (наклон головы влево/вправо)
            
            // Правильный порядок вращения для корректного отображения:
            // 1. Сначала Yaw (поворот головы)
            // 2. Затем Pitch (наклон вперед/назад)
            // 3. Наконец Roll (наклон вбок)
            
            cube.style.transform = 
                `rotateY(${smoothYaw}deg) rotateX(${smoothPitch}deg) rotateZ(${smoothRoll}deg)`;
            
            // Теперь при YAW=0 и PITCH=90 FACE смотрит вверх
            // При YAW=90 и PITCH=90 FACE также смотрит вверх (а не RIGHT)
        }
        
        function smoothValue(axis, value) {
            // Add new value to history
            orientationHistory[axis].push(value);
            
            // Keep only last 5 values
            if (orientationHistory[axis].length > 5) {
                orientationHistory[axis].shift();
            }
            
            // Calculate average
            const sum = orientationHistory[axis].reduce((a, b) => a + b, 0);
            return sum / orientationHistory[axis].length;
        }
        
        function updateSystemStatus(data) {
            // Update device state
            document.getElementById('deviceState').textContent = 
                data.idle ? 'Idle' : 'Active';
            document.getElementById('deviceState').style.color = 
                data.idle ? '#ff9800' : '#4CAF50';
            
            // Update idle status
            document.getElementById('idleStatus').textContent = 
                data.idle ? 'Yes' : 'No';
            document.getElementById('idleStatus').style.color = 
                data.idle ? '#ff9800' : '#666';
            
            // Update zero point status
            if (data.zeroSet) {
                document.getElementById('zeroStatus').textContent = 'Set';
                document.getElementById('zeroStatus').style.color = '#4CAF50';
            } else {
                document.getElementById('zeroStatus').textContent = 'Not Set';
                document.getElementById('zeroStatus').style.color = '#f44336';
            }
            
            // Update WiFi signal if available
            if (data.signal) {
                document.getElementById('wifiSignal').textContent = data.signal + ' dBm';
                const signalColor = data.signal > -60 ? '#4CAF50' : data.signal > -70 ? '#ff9800' : '#f44336';
                document.getElementById('wifiSignal').style.color = signalColor;
            }
        }
        
        function setZeroPoint() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({type: 'setZero'}));
                showNotification('Setting zero point...', 'info');
            } else {
                showNotification('Not connected to sensor', 'error');
            }
        }
        
        function resetZeroPoint() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({type: 'resetZero'}));
                showNotification('Resetting zero point...', 'info');
            } else {
                showNotification('Not connected to sensor', 'error');
            }
        }
        
        function recalibrate() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({type: 'recalibrate'}));
                showNotification('Recalibrating gyro... Don\'t move the sensor!', 'warning');
            } else {
                showNotification('Not connected to sensor', 'error');
            }
        }
        
        function resetYaw() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({type: 'resetYaw'}));
                showNotification('Resetting yaw...', 'info');
            } else {
                showNotification('Not connected to sensor', 'error');
            }
        }
        
        function resetAccumulated() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({type: 'resetAccumulated'}));
                showNotification('Resetting accumulated angles...', 'info');
            } else {
                showNotification('Not connected to sensor', 'error');
            }
        }
        
        function showNotification(message, type = 'info') {
            // Remove existing notifications
            const existingNotifications = document.querySelectorAll('.notification');
            existingNotifications.forEach(notif => notif.remove());
            
            // Create new notification
            const notification = document.createElement('div');
            notification.textContent = message;
            notification.className = 'notification';
            
            // Set color based on type
            if (type === 'error') {
                notification.style.background = '#f44336';
            } else if (type === 'warning') {
                notification.style.background = '#ff9800';
            } else if (type === 'success') {
                notification.style.background = '#4CAF50';
            } else {
                notification.style.background = '#2196F3';
            }
            
            document.body.appendChild(notification);
            
            // Remove after 3 seconds
            setTimeout(() => {
                if (notification.parentNode) {
                    notification.parentNode.removeChild(notification);
                }
            }, 3000);
        }
        
        // Initialize when page loads
        window.addEventListener('load', function() {
            loadSavedIP();
            
            // Show config panel by default if no IP is set
            if (!currentIP) {
                configPanel.style.display = 'block';
            } else {
                // Auto-connect if IP is already configured
                connectWebSocket();
            }
            
            // Set up periodic ping to keep connection alive
            setInterval(() => {
                if (ws && ws.readyState === WebSocket.OPEN) {
                    ws.send(JSON.stringify({
                        type: 'ping',
                        timestamp: Date.now()
                    }));
                }
            }, 30000);
        });
        
        // Reconnect when page becomes visible again
        document.addEventListener('visibilitychange', function() {
            if (!document.hidden && (!ws || ws.readyState !== WebSocket.OPEN)) {
                connectWebSocket();
            }
        });
        
        // Allow Enter key to save config
        document.getElementById('ipInput').addEventListener('keypress', function(e) {
            if (e.key === 'Enter') {
                saveConfig();
            }
        });
    </script>
</body>
</html>
)rawliteral";

Adafruit_MPU6050 mpu;


ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Variables for data smoothing and orientation tracking
float smoothedPitch = 0;
float smoothedRoll = 0;
float smoothedYaw = 0;
const float smoothingFactor = 0.3;

// Complementary filter variables
float pitch = 0, roll = 0, yaw = 0;
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
bool calibrated = false;
unsigned long lastTime = 0;
unsigned long calibrationStart = 0;
const unsigned long calibrationTime = 3000;

// Zero point (reference position)
float zeroPitch = 0;
float zeroRoll = 0;
float zeroYaw = 0;
bool zeroSet = false;

// Accumulated angles relative to zero point
float accumulatedPitch = 0;
float accumulatedRoll = 0;
float accumulatedYaw = 0;

// Continuous angle tracking variables
float contPitch = 0, contRoll = 0, contYaw = 0;
float prevAbsPitch = 0, prevAbsRoll = 0, prevAbsYaw = 0;
bool firstMeasurement = true;

// Previous continuous angles for direction detection
float prevContPitch = 0, prevContRoll = 0, prevContYaw = 0;

// Rotation direction (1 for positive, -1 for negative, 0 for no movement)
int pitchDirection = 0;
int rollDirection = 0;
int yawDirection = 0;

// Threshold for detecting significant changes (in degrees)
const float CHANGE_THRESHOLD = 1.0;
float lastSentPitch = 0;
float lastSentRoll = 0;
float lastSentYaw = 0;

// WebSocket connection management
bool clientConnected = false;
unsigned long lastDataSend = 0;
const unsigned long MIN_SEND_INTERVAL = 50; // Minimum 50ms between sends

// Yaw drift compensation
float yawDrift = 0;
const float YAW_DRIFT_COMPENSATION = 0.01; // Small compensation factor

// Serial mode detection
bool serialMode = false;
bool serialConnected = false;
unsigned long lastSerialCheck = 0;
const unsigned long SERIAL_CHECK_INTERVAL = 5000; // Check every 5 seconds

// EEPROM addresses for zero point storage
const int EEPROM_SIZE = 512;
const int ZERO_PITCH_ADDR = 0;
const int ZERO_ROLL_ADDR = sizeof(float);
const int ZERO_YAW_ADDR = sizeof(float) * 2;
const int ZERO_SET_ADDR = sizeof(float) * 3;

// Idle yaw increment variables
unsigned long lastIdleYawIncrement = 0;
const unsigned long IDLE_YAW_INCREMENT_INTERVAL = 5000; // 5 seconds
bool isDeviceIdle = false;
const float IDLE_THRESHOLD = 0.5; // Idle threshold in degrees/second

// Gaze direction calculation
float gazePitch = 0;    // Vertical gaze direction (-90° to +90°)
float gazeYaw = 0;      // Horizontal gaze direction (-180° to +180°)
float gazeRoll = 0;     // Head tilt for gaze
const float MAX_GAZE_PITCH = 30.0;  // Maximum vertical gaze angle
const float MAX_GAZE_YAW = 60.0;    // Maximum horizontal gaze angle

// Head movement smoothing for gaze
float headMovementFiltered = 0;
const float HEAD_MOVEMENT_SMOOTHING = 0.9;

void setup() {
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Load zero point from EEPROM
  loadZeroPoint();
  
  // Initialize serial port but don't wait for connection
  Serial.begin(115200);
  
  // Give a short time for serial connection to establish
  delay(100);
  
  // Check if serial is connected
  checkSerialConnection();
  
  if (serialConnected) {
    Serial.println();
    Serial.println("Starting MPU6050 Head Tracker with Gaze Direction...");
    Serial.println("Mode: SERIAL + WEBSOCKET");
    serialMode = true;
  } else {
    // No serial connection, proceed silently
    serialMode = false;
  }
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  
  if (serialMode) {
    Serial.print("Connecting to WiFi");
  }
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    if (serialMode) {
      Serial.print(".");
    }
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    if (serialMode) {
      Serial.println("\n✅ Connected to WiFi!");
      Serial.print("📡 IP Address: ");
      Serial.println(WiFi.localIP());
      Serial.print("📶 Signal Strength: ");
      Serial.println(WiFi.RSSI());
    }
  } else {
    if (serialMode) {
      Serial.println("\n❌ Failed to connect to WiFi!");
    }
    return;
  }

  // Initialize I2C
  Wire.begin();
  
  // Initialize MPU6050
  if (!mpu.begin()) {
    if (serialMode) {
      Serial.println("❌ Failed to find MPU6050 chip!");
      while (1) {
        delay(1000);
        Serial.println("Please check MPU6050 connection!");
      }
    } else {
      // Without serial, we can't report errors, just hang
      while (1) {
        delay(1000);
      }
    }
  }
  
  if (serialMode) {
    Serial.println("✅ MPU6050 initialized");
  }
  
  // Configure MPU6050 for head tracking
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);    // Reduced for head movements
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);         // Reduced for head movements
  mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);      // Lower bandwidth for smoother head tracking
  
  // Start calibration
  calibrationStart = millis();
  if (serialMode) {
    Serial.println("🔧 Calibrating gyro... Keep head still for 3 seconds!");
    if (zeroSet) {
      Serial.println("✅ Zero point loaded from EEPROM");
    }
  }
  
  // Setup server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/recalibrate", HTTP_GET, handleRecalibrate);
  server.on("/setZero", HTTP_GET, handleSetZero);
  server.on("/resetZero", HTTP_GET, handleResetZero);
  server.on("/resetGaze", HTTP_GET, handleResetGaze);
  server.onNotFound(handleNotFound);
  
  // Enable CORS
  server.enableCORS(true);
  
  // Start servers
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  if (serialMode) {
    Serial.println("✅ HTTP server started on port 80");
    Serial.println("✅ WebSocket server started on port 81");
    Serial.println("🌐 Use this URL: http://" + WiFi.localIP().toString());
    Serial.println("🎯 Head tracking with gaze direction activated");
  }
}

void loop() {
  server.handleClient();
  webSocket.loop();
  
  // Periodically check if serial connection status changed
  unsigned long currentTime = millis();
  if (currentTime - lastSerialCheck >= SERIAL_CHECK_INTERVAL) {
    checkSerialConnection();
    lastSerialCheck = currentTime;
  }
  
  // Process sensor data
  processSensorData();
  
  // Calculate gaze direction based on head orientation
  calculateGazeDirection();
  
  // Check if device is idle
  isDeviceIdle = checkIfDeviceIdle();
  
  // If device is idle and zero point is set, handle idle yaw increment
  if (isDeviceIdle && zeroSet) {
    handleIdleYawIncrement();
  }
  
  // Send data only if client is connected via WebSocket
  if (clientConnected) {
    if (currentTime - lastDataSend >= MIN_SEND_INTERVAL) {
      sendOrientationData(currentTime);
      lastDataSend = currentTime;
    }
  }
  
  // Always output to serial if connected
  if (serialConnected) {
    outputSerialData(currentTime);
  }
  
  delay(10); // Small delay for stability
}

void calculateGazeDirection() {
  // Calculate head movement intensity for gaze stabilization
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    return;
  }
  
  // Calculate head movement magnitude
  float headMovement = sqrt(g.gyro.x * g.gyro.x + g.gyro.y * g.gyro.y + g.gyro.z * g.gyro.z);
  headMovementFiltered = headMovementFiltered * HEAD_MOVEMENT_SMOOTHING + 
                        headMovement * (1 - HEAD_MOVEMENT_SMOOTHING);
  
  // Gaze direction is based on head orientation with some filtering
  // During rapid head movements, gaze direction is less affected
  float gazeSmoothing = 0.7;
  if (headMovementFiltered > 0.5) {
    gazeSmoothing = 0.3; // Less smoothing during rapid movements
  }
  
  // Calculate gaze direction from head orientation
  // Pitch affects vertical gaze (looking up/down)
  float targetGazePitch = constrain(smoothedPitch, -MAX_GAZE_PITCH, MAX_GAZE_PITCH);
  float targetGazeYaw = constrain(smoothedYaw, -MAX_GAZE_YAW, MAX_GAZE_YAW);
  
  gazePitch = gazePitch * gazeSmoothing + targetGazePitch * (1 - gazeSmoothing);
  gazeYaw = gazeYaw * gazeSmoothing + targetGazeYaw * (1 - gazeSmoothing);
  gazeRoll = gazeRoll * gazeSmoothing + smoothedRoll * (1 - gazeSmoothing);
  
  // Apply zero point correction if set
  if (zeroSet) {
    gazePitch = gazePitch - zeroPitch;
    gazeYaw = gazeYaw - zeroYaw;
    gazeRoll = gazeRoll - zeroRoll;
  }
  
  // Normalize gaze angles
  gazePitch = constrain(gazePitch, -MAX_GAZE_PITCH, MAX_GAZE_PITCH);
  gazeYaw = constrain(gazeYaw, -MAX_GAZE_YAW, MAX_GAZE_YAW);
  
  // Keep roll within reasonable bounds
  while (gazeRoll > 180) gazeRoll -= 360;
  while (gazeRoll < -180) gazeRoll += 360;
}

bool checkIfDeviceIdle() {
  // Check if device is moving (using gyro data)
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    return false;
  }
  
  // Compensate for offsets
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  // Convert to degrees per second
  float gyroXDeg = gyroX * 180.0 / PI;
  float gyroYDeg = gyroY * 180.0 / PI;
  float gyroZDeg = gyroZ * 180.0 / PI;
  
  // If all angular velocities are below threshold - device is idle
  return (abs(gyroXDeg) < IDLE_THRESHOLD && 
          abs(gyroYDeg) < IDLE_THRESHOLD && 
          abs(gyroZDeg) < IDLE_THRESHOLD);
}

void handleIdleYawIncrement() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastIdleYawIncrement >= IDLE_YAW_INCREMENT_INTERVAL) {
    lastIdleYawIncrement = currentTime;
    
    // Add 1 degree to accumulated Yaw
    accumulatedYaw += 1.0;
    
    // Also update continuous yaw for correct tracking
    contYaw += 1.0;
    prevContYaw += 1.0;
    
    if (serialMode) {
      Serial.printf("🔄 Idle Yaw increment: +1.0° | New accumulated Yaw: %.1f°\n", accumulatedYaw);
    }
    
    // Update direction
    yawDirection = 1;
    
    // Reset timer for next direction update
    lastDataSend = currentTime - MIN_SEND_INTERVAL;
  }
}

void loadZeroPoint() {
  EEPROM.get(ZERO_PITCH_ADDR, zeroPitch);
  EEPROM.get(ZERO_ROLL_ADDR, zeroRoll);
  EEPROM.get(ZERO_YAW_ADDR, zeroYaw);
  
  byte zeroFlag;
  EEPROM.get(ZERO_SET_ADDR, zeroFlag);
  zeroSet = (zeroFlag == 1);
  
  if (serialMode && zeroSet) {
    Serial.printf("📁 Loaded zero point - Pitch:%.1f° Roll:%.1f° Yaw:%.1f°\n", 
                 zeroPitch, zeroRoll, zeroYaw);
  }
}

void saveZeroPoint() {
  zeroPitch = smoothedPitch;
  zeroRoll = smoothedRoll;
  zeroYaw = smoothedYaw;
  zeroSet = true;
  
  // Reset accumulated angles when setting new zero point
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  
  // Reset continuous tracking
  contPitch = smoothedPitch;
  contRoll = smoothedRoll;
  contYaw = smoothedYaw;
  prevAbsPitch = smoothedPitch;
  prevAbsRoll = smoothedRoll;
  prevAbsYaw = smoothedYaw;
  prevContPitch = 0;
  prevContRoll = 0;
  prevContYaw = 0;
  firstMeasurement = true;
  
  // Reset gaze direction
  gazePitch = 0;
  gazeYaw = 0;
  gazeRoll = 0;
  
  // Reset direction
  pitchDirection = 0;
  rollDirection = 0;
  yawDirection = 0;
  
  EEPROM.put(ZERO_PITCH_ADDR, zeroPitch);
  EEPROM.put(ZERO_ROLL_ADDR, zeroRoll);
  EEPROM.put(ZERO_YAW_ADDR, zeroYaw);
  EEPROM.put(ZERO_SET_ADDR, (byte)1);
  EEPROM.commit();
  
  if (serialMode) {
    Serial.printf("💾 Zero point saved - Pitch:%.1f° Roll:%.1f° Yaw:%.1f°\n", 
                 zeroPitch, zeroRoll, zeroYaw);
    Serial.println("🔄 Accumulated angles and gaze direction reset to zero");
  }
}

void resetZeroPoint() {
  zeroPitch = 0;
  zeroRoll = 0;
  zeroYaw = 0;
  zeroSet = false;
  
  // Reset accumulated angles when resetting zero point
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  
  // Reset continuous tracking
  contPitch = 0;
  contRoll = 0;
  contYaw = 0;
  prevAbsPitch = 0;
  prevAbsRoll = 0;
  prevAbsYaw = 0;
  prevContPitch = 0;
  prevContRoll = 0;
  prevContYaw = 0;
  firstMeasurement = true;
  
  // Reset gaze direction
  gazePitch = 0;
  gazeYaw = 0;
  gazeRoll = 0;
  
  // Reset direction
  pitchDirection = 0;
  rollDirection = 0;
  yawDirection = 0;
  
  EEPROM.put(ZERO_SET_ADDR, (byte)0);
  EEPROM.commit();
  
  if (serialMode) {
    Serial.println("🔄 Zero point reset");
    Serial.println("🔄 Accumulated angles and gaze direction reset to zero");
  }
}

void resetGazeDirection() {
  gazePitch = 0;
  gazeYaw = 0;
  gazeRoll = 0;
  
  if (serialMode) {
    Serial.println("🔄 Gaze direction reset to center");
  }
}

float calculateRelativeAngle(float absoluteAngle, float zeroAngle) {
  float relative = absoluteAngle - zeroAngle;
  // Normalize to -180 to 180 degrees
  while (relative > 180) relative -= 360;
  while (relative < -180) relative += 360;
  return relative;
}

float calculateContinuousRelativeAngle(float absoluteAngle, float zeroAngle, float &prevAbsolute, float &continuousAngle) {
  if (firstMeasurement) {
    prevAbsolute = absoluteAngle;
    continuousAngle = absoluteAngle;
    return absoluteAngle - zeroAngle;
  }
  
  float delta = absoluteAngle - prevAbsolute;
  
  // Critical correction for transitions through ±180° boundary
  if (delta > 180) {
    delta -= 360;
  } else if (delta < -180) {
    delta += 360;
  }
  
  continuousAngle += delta;
  prevAbsolute = absoluteAngle;
  
  return continuousAngle - zeroAngle;
}

void updateAccumulatedAngles() {
  // Calculate continuous relative angles (accounts for zero-crossing)
  float continuousRelPitch = calculateContinuousRelativeAngle(smoothedPitch, zeroPitch, prevAbsPitch, contPitch);
  float continuousRelRoll = calculateContinuousRelativeAngle(smoothedRoll, zeroRoll, prevAbsRoll, contRoll);
  float continuousRelYaw = calculateContinuousRelativeAngle(smoothedYaw, zeroYaw, prevAbsYaw, contYaw);
  
  // Calculate deltas for direction detection
  float deltaPitch = continuousRelPitch - prevContPitch;
  float deltaRoll = continuousRelRoll - prevContRoll;
  float deltaYaw = continuousRelYaw - prevContYaw;
  
  // Determine direction
  pitchDirection = (abs(deltaPitch) < 0.5) ? 0 : ((deltaPitch > 0) ? 1 : -1);
  rollDirection = (abs(deltaRoll) < 0.5) ? 0 : ((deltaRoll > 0) ? 1 : -1);
  yawDirection = (abs(deltaYaw) < 0.5) ? 0 : ((deltaYaw > 0) ? 1 : -1);
  
  // Accumulated angles are the continuous relative angles
  accumulatedPitch = continuousRelPitch;
  accumulatedRoll = continuousRelRoll;
  accumulatedYaw = continuousRelYaw;
  
  // Update previous continuous angles
  prevContPitch = continuousRelPitch;
  prevContRoll = continuousRelRoll;
  prevContYaw = continuousRelYaw;
  
  firstMeasurement = false;
}

void checkSerialConnection() {
  // Try to write to serial and see if it responds
  bool previouslyConnected = serialConnected;
  
  // Simple check: if we can write to serial without error, it's probably connected
  serialConnected = (Serial);
  
  if (serialConnected && !previouslyConnected) {
    // Serial just connected
    serialMode = true;
    Serial.println("\n🔌 Serial connected - Switching to SERIAL + WEBSOCKET mode");
    Serial.println("🎯 MPU6050 Head Tracker with Gaze Direction Active");
    Serial.println("📡 IP: " + WiFi.localIP().toString());
  } else if (!serialConnected && previouslyConnected) {
    // Serial just disconnected
    serialMode = false;
    // No way to print this since serial is disconnected
  }
}

void outputSerialData(unsigned long currentTime) {
  static unsigned long lastSerialOutput = 0;
  const unsigned long SERIAL_OUTPUT_INTERVAL = 100; // Output every 100ms
  
  if (currentTime - lastSerialOutput >= SERIAL_OUTPUT_INTERVAL) {
    lastSerialOutput = currentTime;
    
    float relPitch = calculateRelativeAngle(smoothedPitch, zeroPitch);
    float relRoll = calculateRelativeAngle(smoothedRoll, zeroRoll);
    float relYaw = calculateRelativeAngle(smoothedYaw, zeroYaw);
    
    Serial.printf("HEAD Pitch:%.1f° Roll:%.1f° Yaw:%.1f° | ", 
                 smoothedPitch, smoothedRoll, smoothedYaw);
    Serial.printf("GAZE Pitch:%.1f° Yaw:%.1f° Roll:%.1f° | ", 
                 gazePitch, gazeYaw, gazeRoll);
    Serial.printf("REL Pitch:%.1f° Roll:%.1f° Yaw:%.1f° | ", 
                 relPitch, relRoll, relYaw);
    Serial.printf("ACC Pitch:%.1f° Roll:%.1f° Yaw:%.1f° | ", 
                 accumulatedPitch, accumulatedRoll, accumulatedYaw);
    Serial.printf("DIR Pitch:%d Roll:%d Yaw:%d | ", 
                 pitchDirection, rollDirection, yawDirection);
    Serial.printf("Idle:%s | ", isDeviceIdle ? "YES" : "NO");
    Serial.printf("Clients:%d RSSI:%ddBm\n", 
                 webSocket.connectedClients(), WiFi.RSSI());
  }
}

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
    
    // Show calibration progress only in serial mode
    if (serialMode && sampleCount % 50 == 0) {
      int progress = (millis() - calibrationStart) * 100 / calibrationTime;
      Serial.printf("Calibration progress: %d%%\n", progress);
    }
  } else {
    gyroOffsetX = sumX / sampleCount;
    gyroOffsetY = sumY / sampleCount;
    gyroOffsetZ = sumZ / sampleCount;
    calibrated = true;
    
    // Calculate initial yaw drift compensation
    yawDrift = gyroOffsetZ; // Use Z-axis offset as initial drift estimate
    
    if (serialMode) {
      Serial.println("✅ Gyro calibration complete!");
      Serial.printf("Offsets - X:%.6f, Y:%.6f, Z:%.6f\n", gyroOffsetX, gyroOffsetY, gyroOffsetZ);
      Serial.printf("Yaw drift compensation: %.6f\n", yawDrift);
      Serial.printf("Samples processed: %d\n", sampleCount);
    }
    
    // Notify all clients
    webSocket.broadcastTXT("{\"type\":\"status\",\"message\":\"Calibration complete\"}");
  }
}

void processSensorData() {
  if (!calibrated) {
    calibrateGyro();
    return;
  }
  
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    if (serialMode) {
      Serial.println("Error reading MPU6050 data");
    }
    return;
  }
  
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  if (lastTime == 0) {
    deltaTime = 0.01; // Initial small value
  }
  lastTime = currentTime;
  
  // Compensate for gyro offset
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  // Calculate angles from accelerometer (MPU6050_004 logic)
  float accelPitch = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
  float accelRoll = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
  
  // Apply yaw drift compensation
  if (abs(gyroZ) < 0.01) { // Only compensate when gyro is nearly stationary
    gyroZ -= yawDrift * YAW_DRIFT_COMPENSATION;
  }
  
  // Gyro integration
  pitch += gyroX * deltaTime * 180.0 / PI;
  roll += gyroY * deltaTime * 180.0 / PI;
  yaw += gyroZ * deltaTime * 180.0 / PI;
  
  // Complementary filter for pitch and roll (MPU6050_004 logic)
  float alpha = 0.96;
  pitch = alpha * pitch + (1.0 - alpha) * accelPitch;
  roll = alpha * roll + (1.0 - alpha) * accelRoll;
  
  // Additional yaw stabilization when device is relatively stable (MPU6050_004 logic)
  float totalAccel = sqrt(a.acceleration.x * a.acceleration.x + 
                         a.acceleration.y * a.acceleration.y + 
                         a.acceleration.z * a.acceleration.z);
  
  // If device is relatively stable (not moving much), apply small yaw correction
  if (abs(totalAccel - 9.8) < 0.5 && abs(gyroZ) < 0.005) {
    yaw *= 0.999; // Very slow decay to prevent drift
  }
  
  // Apply additional smoothing for display
  smoothedPitch = smoothedPitch * (1 - smoothingFactor) + pitch * smoothingFactor;
  smoothedRoll = smoothedRoll * (1 - smoothingFactor) + roll * smoothingFactor;
  smoothedYaw = smoothedYaw * (1 - smoothingFactor) + yaw * smoothingFactor;
}

void sendOrientationData(unsigned long currentTime) {
  // Check if data has changed significantly
  bool pitchChanged = abs(smoothedPitch - lastSentPitch) >= CHANGE_THRESHOLD;
  bool rollChanged = abs(smoothedRoll - lastSentRoll) >= CHANGE_THRESHOLD;
  bool yawChanged = abs(smoothedYaw - lastSentYaw) >= CHANGE_THRESHOLD;
  
  // Always send data if it's the first time or if any value changed
  if (pitchChanged || rollChanged || yawChanged || lastDataSend == 0) {
    // Update accumulated angles with zero-crossing detection
    updateAccumulatedAngles();
    
    // Calculate relative angles (for backward compatibility)
    float relPitch = calculateRelativeAngle(smoothedPitch, zeroPitch);
    float relRoll = calculateRelativeAngle(smoothedRoll, zeroRoll);
    float relYaw = calculateRelativeAngle(smoothedYaw, zeroYaw);
    
    // Create JSON message with both absolute and relative values
    String json = "{";
    json += "\"type\":\"sensorData\",";
    json += "\"pitch\":" + String(relPitch, 2) + ",";        // Relative pitch (backward compatibility)
    json += "\"roll\":" + String(relRoll, 2) + ",";          // Relative roll (backward compatibility)
    json += "\"yaw\":" + String(relYaw, 2) + ",";            // Relative yaw (backward compatibility)
    json += "\"absPitch\":" + String(smoothedPitch, 2) + ","; // Absolute pitch
    json += "\"absRoll\":" + String(smoothedRoll, 2) + ",";   // Absolute roll
    json += "\"absYaw\":" + String(smoothedYaw, 2) + ",";     // Absolute yaw
    json += "\"accPitch\":" + String(accumulatedPitch, 2) + ","; // Accumulated pitch
    json += "\"accRoll\":" + String(accumulatedRoll, 2) + ",";   // Accumulated roll
    json += "\"accYaw\":" + String(accumulatedYaw, 2) + ",";     // Accumulated yaw
    json += "\"gazePitch\":" + String(gazePitch, 2) + ",";    // Gaze pitch
    json += "\"gazeYaw\":" + String(gazeYaw, 2) + ",";        // Gaze yaw
    json += "\"gazeRoll\":" + String(gazeRoll, 2) + ",";      // Gaze roll
    json += "\"dirPitch\":" + String(pitchDirection) + ",";   // Pitch direction
    json += "\"dirRoll\":" + String(rollDirection) + ",";     // Roll direction
    json += "\"dirYaw\":" + String(yawDirection) + ",";       // Yaw direction
    json += "\"zeroPitch\":" + String(zeroPitch, 2) + ",";    // Zero point pitch
    json += "\"zeroRoll\":" + String(zeroRoll, 2) + ",";      // Zero point roll
    json += "\"zeroYaw\":" + String(zeroYaw, 2) + ",";        // Zero point yaw
    json += "\"zeroSet\":" + String(zeroSet ? "true" : "false") + ",";
    json += "\"idle\":" + String(isDeviceIdle ? "true" : "false") + ","; // Idle state
    json += "\"timestamp\":" + String(currentTime);
    json += "}";
    
    // Send to all connected clients
    webSocket.broadcastTXT(json);
    
    // Update last sent values
    lastSentPitch = smoothedPitch;
    lastSentRoll = smoothedRoll;
    lastSentYaw = smoothedYaw;
    
    // Debug output only in serial mode
    if (serialMode) {
      static unsigned long lastDebug = 0;
      if (currentTime - lastDebug >= 1000) {
        lastDebug = currentTime;
        Serial.printf("📤 WebSocket: HEAD[P:%.1f° R:%.1f° Y:%.1f°] GAZE[P:%.1f° Y:%.1f° R:%.1f°] REL[P:%.1f° R:%.1f° Y:%.1f°] ACC[P:%.1f° R:%.1f° Y:%.1f°] DIR[P:%d R:%d Y:%d] Idle:%s\n", 
                     smoothedPitch, smoothedRoll, smoothedYaw, gazePitch, gazeYaw, gazeRoll,
                     relPitch, relRoll, relYaw, accumulatedPitch, accumulatedRoll, accumulatedYaw, 
                     pitchDirection, rollDirection, yawDirection, isDeviceIdle ? "YES" : "NO");
      }
    }
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      if (serialMode) {
        Serial.printf("🔌 [%u] Disconnected!\n", num);
      }
      clientConnected = (webSocket.connectedClients() > 0);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        if (serialMode) {
          Serial.printf("✅ [%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        }
        clientConnected = true;
        
        // Send welcome message
        String welcome = "{\"type\":\"status\",\"message\":\"Connected to MPU6050 Head Tracker with Gaze Direction\"}";
        webSocket.sendTXT(num, welcome);
        
        // Send network info
        String networkInfo = "{\"type\":\"networkInfo\",\"ip\":\"" + 
                            WiFi.localIP().toString() + 
                            "\",\"signal\":" + String(WiFi.RSSI()) + 
                            ",\"ssid\":\"" + String(ssid) + "\"}";
        webSocket.sendTXT(num, networkInfo);
        
        // Send zero point info
        if (zeroSet) {
          String zeroInfo = "{\"type\":\"zeroInfo\",\"zeroPitch\":" + String(zeroPitch, 2) + 
                           ",\"zeroRoll\":" + String(zeroRoll, 2) + 
                           ",\"zeroYaw\":" + String(zeroYaw, 2) + "}";
          webSocket.sendTXT(num, zeroInfo);
        }
      }
      break;
      
    case WStype_TEXT:
      if (serialMode) {
        Serial.printf("📨 [%u] Received: %s\n", num, payload);
      }
      
      // Parse JSON message
      String message = String((char*)payload);
      
      // Handle recalibrate command
      if (message.indexOf("recalibrate") != -1) {
        calibrated = false;
        calibrationStart = millis();
        pitch = roll = yaw = 0;
        yawDrift = 0;
        firstMeasurement = true;
        webSocket.sendTXT(num, "{\"type\":\"status\",\"message\":\"Recalibrating gyro...\"}");
        if (serialMode) {
          Serial.println("Recalibration started");
        }
      }
      // Handle set zero command
      else if (message.indexOf("setZero") != -1) {
        saveZeroPoint();
        webSocket.sendTXT(num, "{\"type\":\"status\",\"message\":\"Zero point set and saved\"}");
        // Broadcast zero point update to all clients
        String zeroInfo = "{\"type\":\"zeroInfo\",\"zeroPitch\":" + String(zeroPitch, 2) + 
                         ",\"zeroRoll\":" + String(zeroRoll, 2) + 
                         ",\"zeroYaw\":" + String(zeroYaw, 2) + "}";
        webSocket.broadcastTXT(zeroInfo);
      }
      // Handle reset zero command
      else if (message.indexOf("resetZero") != -1) {
        resetZeroPoint();
        webSocket.sendTXT(num, "{\"type\":\"status\",\"message\":\"Zero point reset\"}");
        // Broadcast zero reset to all clients
        webSocket.broadcastTXT("{\"type\":\"zeroReset\"}");
      }
      // Handle ping message
      else if (message.indexOf("ping") != -1) {
        String pong = "{\"type\":\"pong\",\"timestamp\":";
        pong += millis();
        pong += "}";
        webSocket.sendTXT(num, pong);
      }
      // Handle network info request
      else if (message.indexOf("networkInfo") != -1) {
        String networkInfo = "{\"type\":\"networkInfo\",\"ip\":\"" + 
                            WiFi.localIP().toString() + 
                            "\",\"signal\":" + String(WiFi.RSSI()) + 
                            ",\"ssid\":\"" + String(ssid) + "\"}";
        webSocket.sendTXT(num, networkInfo);
      }
      // Handle reset gaze command
      else if (message.indexOf("resetGaze") != -1) {
        resetGazeDirection();
        webSocket.sendTXT(num, "{\"type\":\"status\",\"message\":\"Gaze direction reset\"}");
      }
      // Handle reset accumulated angles command
      else if (message.indexOf("resetAccumulated") != -1) {
        accumulatedPitch = 0;
        accumulatedRoll = 0;
        accumulatedYaw = 0;
        webSocket.sendTXT(num, "{\"type\":\"status\",\"message\":\"Accumulated angles reset\"}");
        if (serialMode) {
          Serial.println("Accumulated angles reset");
        }
      }
      // Handle reset yaw command
      else if (message.indexOf("resetYaw") != -1) {
        yaw = 0;
        smoothedYaw = 0;
        accumulatedYaw = 0;
        contYaw = 0;
        prevAbsYaw = 0;
        prevContYaw = 0;
        webSocket.sendTXT(num, "{\"type\":\"status\",\"message\":\"Yaw reset\"}");
        if (serialMode) {
          Serial.println("Yaw reset");
        }
      }
      break;
  }
}

// HTTP Handlers
void handleRoot() {
  String html = FPSTR(HTML_HEAD);
  html += FPSTR(HTML_BODY_START);
  html += FPSTR(HTML_CONFIG_PANEL);
  html += FPSTR(HTML_DASHBOARD_START);
  html += FPSTR(HTML_ABSOLUTE_PANEL);
  html += FPSTR(HTML_RELATIVE_PANEL);
  html += FPSTR(HTML_VISUALIZATION_PANEL);
  html += FPSTR(HTML_DASHBOARD_END);
  html += FPSTR(HTML_CONTROLS);
  html += FPSTR(HTML_SYSTEM_STATUS);
  html += FPSTR(HTML_HOW_TO_USE);
  html += FPSTR(HTML_CONNECTION_INFO);
  html += FPSTR(HTML_BODY_END);
  html += FPSTR(HTML_SCRIPT);
  
  server.send(200, "text/html", html);
}

void handleData() {
  // Calculate relative angles
  float relPitch = calculateRelativeAngle(smoothedPitch, zeroPitch);
  float relRoll = calculateRelativeAngle(smoothedRoll, zeroRoll);
  float relYaw = calculateRelativeAngle(smoothedYaw, zeroYaw);
  
  String json = "{";
  json += "\"pitch\":" + String(relPitch, 2) + ",";
  json += "\"roll\":" + String(relRoll, 2) + ",";
  json += "\"yaw\":" + String(relYaw, 2) + ",";
  json += "\"absPitch\":" + String(smoothedPitch, 2) + ",";
  json += "\"absRoll\":" + String(smoothedRoll, 2) + ",";
  json += "\"absYaw\":" + String(smoothedYaw, 2) + ",";
  json += "\"relPitch\":" + String(relPitch, 2) + ",";
  json += "\"relRoll\":" + String(relRoll, 2) + ",";
  json += "\"relYaw\":" + String(relYaw, 2) + ",";
  json += "\"accPitch\":" + String(accumulatedPitch, 2) + ",";
  json += "\"accRoll\":" + String(accumulatedRoll, 2) + ",";
  json += "\"accYaw\":" + String(accumulatedYaw, 2) + ",";
  json += "\"gazePitch\":" + String(gazePitch, 2) + ",";
  json += "\"gazeYaw\":" + String(gazeYaw, 2) + ",";
  json += "\"gazeRoll\":" + String(gazeRoll, 2) + ",";
  json += "\"dirPitch\":" + String(pitchDirection) + ",";
  json += "\"dirRoll\":" + String(rollDirection) + ",";
  json += "\"dirYaw\":" + String(yawDirection) + ",";
  json += "\"zeroPitch\":" + String(zeroPitch, 2) + ",";
  json += "\"zeroRoll\":" + String(zeroRoll, 2) + ",";
  json += "\"zeroYaw\":" + String(zeroYaw, 2) + ",";
  json += "\"zeroSet\":" + String(zeroSet ? "true" : "false") + ",";
  json += "\"idle\":" + String(isDeviceIdle ? "true" : "false") + ",";
  json += "\"signal\":" + String(WiFi.RSSI()) + ",";
  json += "\"timestamp\":" + String(millis());
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleStatus() {
  String json = "{";
  json += "\"status\":\"running\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"ssid\":\"" + String(ssid) + "\",";
  json += "\"signal\":" + String(WiFi.RSSI()) + ",";
  json += "\"clients\":" + String(webSocket.connectedClients()) + ",";
  json += "\"calibrated\":" + String(calibrated ? "true" : "false") + ",";
  json += "\"zeroSet\":" + String(zeroSet ? "true" : "false") + ",";
  json += "\"serialMode\":" + String(serialMode ? "true" : "false") + ",";
  json += "\"uptime\":" + String(millis());
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleRecalibrate() {
  calibrated = false;
  calibrationStart = millis();
  pitch = roll = yaw = 0;
  yawDrift = 0;
  firstMeasurement = true;
  
  String json = "{\"status\":\"recalibrating\",\"message\":\"Gyro recalibration started\"}";
  server.send(200, "application/json", json);
  
  if (serialMode) {
    Serial.println("Recalibration started via HTTP");
  }
}

void handleSetZero() {
  saveZeroPoint();
  
  String json = "{\"status\":\"success\",\"message\":\"Zero point set and saved\"}";
  server.send(200, "application/json", json);
  
  // Broadcast to WebSocket clients
  String zeroInfo = "{\"type\":\"zeroInfo\",\"zeroPitch\":" + String(zeroPitch, 2) + 
                   ",\"zeroRoll\":" + String(zeroRoll, 2) + 
                   ",\"zeroYaw\":" + String(zeroYaw, 2) + "}";
  webSocket.broadcastTXT(zeroInfo);
}

void handleResetZero() {
  resetZeroPoint();
  
  String json = "{\"status\":\"success\",\"message\":\"Zero point reset\"}";
  server.send(200, "application/json", json);
  
  // Broadcast to WebSocket clients
  webSocket.broadcastTXT("{\"type\":\"zeroReset\"}");
}

void handleResetGaze() {
  resetGazeDirection();
  
  String json = "{\"status\":\"success\",\"message\":\"Gaze direction reset to center\"}";
  server.send(200, "application/json", json);
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
