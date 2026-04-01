/**
 * @file native_peripheral.ino
 * @brief Flushes data between Native USB Serial and UART Serial
 * @date 4/1/26
 * @author Aldem Pido
 */

#include "USB.h"
#include "USBCDC.h"

USBCDC USBSerial;

void setup() {
  Serial.begin(115200);
  USBSerial.begin();
  USB.begin();
  delay(1000);
}

void loop() {
  // USB to Serial
  if (USBSerial.available()) {
    Serial.write(USBSerial.read());
    Serial.flush();
  }
  
  // Serial to USB
  if (Serial.available()) {
    USBSerial.write(Serial.read());
    USBSerial.flush();
  }
}