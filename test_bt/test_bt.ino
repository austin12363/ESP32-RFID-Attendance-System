#include <WiFi.h>
#include <WebServer.h>
#include <BluetoothSerial.h>

BluetoothSerial SerialBT;
WebServer server(80);

void setup() {
  Serial.begin(115200);
  WiFi.softAP("Test-AP");
  server.begin();
  SerialBT.begin("ESP32-Attendance");
}

void loop() {
  server.handleClient();
  if (SerialBT.available()) {
    char c = SerialBT.read();
    SerialBT.write(c);
  }
}
