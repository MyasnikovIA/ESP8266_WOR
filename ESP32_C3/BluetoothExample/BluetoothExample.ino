#include <BluetoothSerial.h>
BluetoothSerial BT;

void setup() {
  Serial.begin(115200);
  BT.begin("VR_head_222");
  Serial.println("Bluetooth ready! Connect to 'VR_head'");
}

void loop() {
  // Чтение из Bluetooth
  if (BT.available()) {
    while (BT.available()) {
      char c = BT.read();
      Serial.write(c);
    }
    Serial.println();
  }
  
  // Запись в Bluetooth
  if (Serial.available()) {
    delay(10);
    String data = Serial.readString();
    BT.print("-----ESP32: ");
    BT.println(data);
  }
}
