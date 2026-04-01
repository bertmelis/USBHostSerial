/**
 * @file native_host.ino
 * @brief Example sketch for USBHostSerial library demonstrating native USB host functionality.
 * @date 4/1/26
 * @author Aldem Pido
 * 
 * Tested with 2ESP32 S3 Wroom 2 Boards with UART and USB ports.
 * 
 * Host Board
 * USB -> Peripheral USB
 * UART -> Computer Serial Monitor
 * 
 * Peripheral Board
 * USB -> Peripheral USB
 * UART -> Computer Serial Monitor
 */
#define USBHOSTSERIAL_BUFFERSIZE 512
#include <Arduino.h>
#include <USBHostSerial.h>

USBHostSerial hostSerial;

// Logger function for USBHostSerial
void usbHostLogger(const char* msg) {
  Serial.print("[USBHostSerial] ");
  Serial.println(msg);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  hostSerial.begin(115200, 0, 0, 8); // baudrate, stopbits, parity, databits
  hostSerial.setLogger(usbHostLogger);  // Enable logging
}

void loop() {
  // Host to Serial
  if (hostSerial.available()) {
    Serial.write(hostSerial.read());
    Serial.flush();
  }
  
  // Serial to Host
  if (Serial.available()) {
    hostSerial.write(Serial.read());
  }
}