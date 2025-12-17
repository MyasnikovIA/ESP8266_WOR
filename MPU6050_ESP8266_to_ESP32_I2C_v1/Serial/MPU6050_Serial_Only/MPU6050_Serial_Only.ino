
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

Adafruit_MPU6050 mpu;

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

// Yaw drift compensation
float yawDrift = 0;
const float YAW_DRIFT_COMPENSATION = 0.01;

// Idle yaw increment variables
unsigned long lastIdleYawIncrement = 0;
const unsigned long IDLE_YAW_INCREMENT_INTERVAL = 5000;
bool isDeviceIdle = false;
const float IDLE_THRESHOLD = 0.5;

// Gaze direction calculation
float gazePitch = 0;
float gazeYaw = 0;
float gazeRoll = 0;
const float MAX_GAZE_PITCH = 30.0;
const float MAX_GAZE_YAW = 60.0;

// Head movement smoothing for gaze
float headMovementFiltered = 0;
const float HEAD_MOVEMENT_SMOOTHING = 0.9;

// Data output interval
unsigned long lastSerialOutput = 0;
const unsigned long SERIAL_OUTPUT_INTERVAL = 50; // Send data every 50ms (20Hz)

void setup() {
  // Initialize serial port
  Serial.begin(115200);
  delay(100); // Wait for serial to initialize
  
  Serial.println();
  Serial.println("========================================");
  Serial.println("MPU6050 Head Tracker - Serial Only Mode");
  Serial.println("========================================");
  Serial.println("Starting initialization...");
  
  // Initialize I2C
  Wire.begin();
  
  // Initialize MPU6050
  if (!mpu.begin()) {
    Serial.println("❌ Failed to find MPU6050 chip!");
    Serial.println("Please check MPU6050 connection!");
    while (1) {
      delay(1000);
    }
  }
  
  Serial.println("✅ MPU6050 initialized");
  
  // Configure MPU6050 for head tracking
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
  
  // Start calibration
  calibrationStart = millis();
  Serial.println("🔧 Calibrating gyro... Keep head still for 3 seconds!");
  
  Serial.println("✅ System ready!");
  Serial.println("📤 Data format: JSON via Serial @ 115200 baud");
  Serial.println("========================================");
}

void loop() {
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
  
  // Send data via Serial at fixed interval
  unsigned long currentTime = millis();
  if (currentTime - lastSerialOutput >= SERIAL_OUTPUT_INTERVAL) {
    sendOrientationData(currentTime);
    lastSerialOutput = currentTime;
  }
  
  // Check for serial commands
  checkSerialCommands();
  
  delay(10); // Small delay for stability
}

void calculateGazeDirection() {
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    return;
  }
  
  float headMovement = sqrt(g.gyro.x * g.gyro.x + g.gyro.y * g.gyro.y + g.gyro.z * g.gyro.z);
  headMovementFiltered = headMovementFiltered * HEAD_MOVEMENT_SMOOTHING + 
                        headMovement * (1 - HEAD_MOVEMENT_SMOOTHING);
  
  float gazeSmoothing = 0.7;
  if (headMovementFiltered > 0.5) {
    gazeSmoothing = 0.3;
  }
  
  float targetGazePitch = constrain(smoothedPitch, -MAX_GAZE_PITCH, MAX_GAZE_PITCH);
  float targetGazeYaw = constrain(smoothedYaw, -MAX_GAZE_YAW, MAX_GAZE_YAW);
  
  gazePitch = gazePitch * gazeSmoothing + targetGazePitch * (1 - gazeSmoothing);
  gazeYaw = gazeYaw * gazeSmoothing + targetGazeYaw * (1 - gazeSmoothing);
  gazeRoll = gazeRoll * gazeSmoothing + smoothedRoll * (1 - gazeSmoothing);
  
  if (zeroSet) {
    gazePitch = gazePitch - zeroPitch;
    gazeYaw = gazeYaw - zeroYaw;
    gazeRoll = gazeRoll - zeroRoll;
  }
  
  gazePitch = constrain(gazePitch, -MAX_GAZE_PITCH, MAX_GAZE_PITCH);
  gazeYaw = constrain(gazeYaw, -MAX_GAZE_YAW, MAX_GAZE_YAW);
  
  while (gazeRoll > 180) gazeRoll -= 360;
  while (gazeRoll < -180) gazeRoll += 360;
}

bool checkIfDeviceIdle() {
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    return false;
  }
  
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  float gyroXDeg = gyroX * 180.0 / PI;
  float gyroYDeg = gyroY * 180.0 / PI;
  float gyroZDeg = gyroZ * 180.0 / PI;
  
  return (abs(gyroXDeg) < IDLE_THRESHOLD && 
          abs(gyroYDeg) < IDLE_THRESHOLD && 
          abs(gyroZDeg) < IDLE_THRESHOLD);
}

void handleIdleYawIncrement() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastIdleYawIncrement >= IDLE_YAW_INCREMENT_INTERVAL) {
    lastIdleYawIncrement = currentTime;
    
    accumulatedYaw += 1.0;
    contYaw += 1.0;
    prevContYaw += 1.0;
    yawDirection = 1;
  }
}

float calculateRelativeAngle(float absoluteAngle, float zeroAngle) {
  float relative = absoluteAngle - zeroAngle;
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
  float continuousRelPitch = calculateContinuousRelativeAngle(smoothedPitch, zeroPitch, prevAbsPitch, contPitch);
  float continuousRelRoll = calculateContinuousRelativeAngle(smoothedRoll, zeroRoll, prevAbsRoll, contRoll);
  float continuousRelYaw = calculateContinuousRelativeAngle(smoothedYaw, zeroYaw, prevAbsYaw, contYaw);
  
  float deltaPitch = continuousRelPitch - prevContPitch;
  float deltaRoll = continuousRelRoll - prevContRoll;
  float deltaYaw = continuousRelYaw - prevContYaw;
  
  pitchDirection = (abs(deltaPitch) < 0.5) ? 0 : ((deltaPitch > 0) ? 1 : -1);
  rollDirection = (abs(deltaRoll) < 0.5) ? 0 : ((deltaRoll > 0) ? 1 : -1);
  yawDirection = (abs(deltaYaw) < 0.5) ? 0 : ((deltaYaw > 0) ? 1 : -1);
  
  accumulatedPitch = continuousRelPitch;
  accumulatedRoll = continuousRelRoll;
  accumulatedYaw = continuousRelYaw;
  
  prevContPitch = continuousRelPitch;
  prevContRoll = continuousRelRoll;
  prevContYaw = continuousRelYaw;
  
  firstMeasurement = false;
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
    
    if (sampleCount % 50 == 0) {
      int progress = (millis() - calibrationStart) * 100 / calibrationTime;
      Serial.printf("Calibration progress: %d%%\n", progress);
    }
  } else {
    gyroOffsetX = sumX / sampleCount;
    gyroOffsetY = sumY / sampleCount;
    gyroOffsetZ = sumZ / sampleCount;
    calibrated = true;
    
    yawDrift = gyroOffsetZ;
    
    Serial.println("✅ Gyro calibration complete!");
    Serial.printf("Offsets - X:%.6f, Y:%.6f, Z:%.6f\n", gyroOffsetX, gyroOffsetY, gyroOffsetZ);
    Serial.printf("Yaw drift compensation: %.6f\n", yawDrift);
    Serial.printf("Samples processed: %d\n", sampleCount);
  }
}

void processSensorData() {
  if (!calibrated) {
    calibrateGyro();
    return;
  }
  
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    Serial.println("Error reading MPU6050 data");
    return;
  }
  
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  if (lastTime == 0) {
    deltaTime = 0.01;
  }
  lastTime = currentTime;
  
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  float accelPitch = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
  float accelRoll = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
  
  if (abs(gyroZ) < 0.01) {
    gyroZ -= yawDrift * YAW_DRIFT_COMPENSATION;
  }
  
  pitch += gyroX * deltaTime * 180.0 / PI;
  roll += gyroY * deltaTime * 180.0 / PI;
  yaw += gyroZ * deltaTime * 180.0 / PI;
  
  float alpha = 0.96;
  pitch = alpha * pitch + (1.0 - alpha) * accelPitch;
  roll = alpha * roll + (1.0 - alpha) * accelRoll;
  
  float totalAccel = sqrt(a.acceleration.x * a.acceleration.x + 
                         a.acceleration.y * a.acceleration.y + 
                         a.acceleration.z * a.acceleration.z);
  
  if (abs(totalAccel - 9.8) < 0.5 && abs(gyroZ) < 0.005) {
    yaw *= 0.999;
  }
  
  smoothedPitch = smoothedPitch * (1 - smoothingFactor) + pitch * smoothingFactor;
  smoothedRoll = smoothedRoll * (1 - smoothingFactor) + roll * smoothingFactor;
  smoothedYaw = smoothedYaw * (1 - smoothingFactor) + yaw * smoothingFactor;
}

void sendOrientationData(unsigned long currentTime) {
  updateAccumulatedAngles();
  
  float relPitch = calculateRelativeAngle(smoothedPitch, zeroPitch);
  float relRoll = calculateRelativeAngle(smoothedRoll, zeroRoll);
  float relYaw = calculateRelativeAngle(smoothedYaw, zeroYaw);
  
  // Create JSON message
  String json = "{";
  json += "\"type\":\"sensorData\",";
  json += "\"pitch\":" + String(relPitch, 2) + ",";
  json += "\"roll\":" + String(relRoll, 2) + ",";
  json += "\"yaw\":" + String(relYaw, 2) + ",";
  json += "\"absPitch\":" + String(smoothedPitch, 2) + ",";
  json += "\"absRoll\":" + String(smoothedRoll, 2) + ",";
  json += "\"absYaw\":" + String(smoothedYaw, 2) + ",";
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
  json += "\"timestamp\":" + String(currentTime);
  json += "}";
  
  Serial.println(json);
}

void checkSerialCommands() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "recalibrate") {
      calibrated = false;
      calibrationStart = millis();
      pitch = roll = yaw = 0;
      yawDrift = 0;
      firstMeasurement = true;
      Serial.println("{\"type\":\"status\",\"message\":\"Recalibrating gyro...\"}");
    } 
    else if (command == "setZero") {
      setZeroPoint();
    } 
    else if (command == "resetZero") {
      resetZeroPoint();
    } 
    else if (command == "resetGaze") {
      resetGazeDirection();
    } 
    else if (command == "resetAccumulated") {
      resetAccumulatedAngles();
    } 
    else if (command == "resetYaw") {
      resetYaw();
    }
  }
}

void setZeroPoint() {
  zeroPitch = smoothedPitch;
  zeroRoll = smoothedRoll;
  zeroYaw = smoothedYaw;
  zeroSet = true;
  
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  
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
  
  gazePitch = 0;
  gazeYaw = 0;
  gazeRoll = 0;
  
  pitchDirection = 0;
  rollDirection = 0;
  yawDirection = 0;
  
  Serial.printf("{\"type\":\"zeroInfo\",\"zeroPitch\":%.2f,\"zeroRoll\":%.2f,\"zeroYaw\":%.2f}\n", 
                zeroPitch, zeroRoll, zeroYaw);
  Serial.println("{\"type\":\"status\",\"message\":\"Zero point set\"}");
}

void resetZeroPoint() {
  zeroPitch = 0;
  zeroRoll = 0;
  zeroYaw = 0;
  zeroSet = false;
  
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  
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
  
  gazePitch = 0;
  gazeYaw = 0;
  gazeRoll = 0;
  
  pitchDirection = 0;
  rollDirection = 0;
  yawDirection = 0;
  
  Serial.println("{\"type\":\"zeroReset\"}");
  Serial.println("{\"type\":\"status\",\"message\":\"Zero point reset\"}");
}

void resetGazeDirection() {
  gazePitch = 0;
  gazeYaw = 0;
  gazeRoll = 0;
  Serial.println("{\"type\":\"status\",\"message\":\"Gaze direction reset\"}");
}

void resetAccumulatedAngles() {
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  Serial.println("{\"type\":\"status\",\"message\":\"Accumulated angles reset\"}");
}

void resetYaw() {
  yaw = 0;
  smoothedYaw = 0;
  accumulatedYaw = 0;
  contYaw = 0;
  prevAbsYaw = 0;
  prevContYaw = 0;
  Serial.println("{\"type\":\"status\",\"message\":\"Yaw reset\"}");
}
