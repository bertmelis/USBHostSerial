#include <Arduino.h>

#include <USBHostSerial.h>

USBHostSerial usbSerial;

void logUSBLib(const char* msg) {
  Serial1.println(msg);
}

void setup() {
  usbSerial.setLogger(logUSBLib);
  /*
  baudrate
  stopbits: 0: 1 stopbit, 1: 1.5 stopbits, 2: 2 stopbits
  parity: 0: None, 1: Odd, 2: Even, 3: Mark, 4: Space
  databits: 8
  */
  usbSerial.begin(9600, 0, 0, 8);
  Serial1.begin(115200);
}

void loop() {
  // send message every 10s
  static uint32_t lastMessage = 0;
  if (millis() - lastMessage > 10000) {
    lastMessage = millis();
    const char message[] = "USB says hello\n";
    usbSerial.write((uint8_t*)message, 15);
  }
  usbSerial.flush(false);  // true: only flush tx (default), false: flush rx and tx
}
