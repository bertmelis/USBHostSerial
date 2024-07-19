#include <Arduino.h>

#include <usb_host_serial.h>

usb_host_serial usbSerial;

void setup() {
  /*
  baudrate
  stopbits: 0: 1 stopbit, 1: 1.5 stopbits, 2: 2 stopbits
  parity: 0: None, 1: Odd, 2: Even, 3: Mark, 4: Space
  databits: 8
  */
  usbSerial.begin(9600, 0, 0, 8);
}

void loop() {
  // send message every 10s
  static uint32_t lastMessage = 0;
  if (millis() - lastMessage > 10000) {
    lastMessage = millis();
    const char message[] = "USB says hello\n";
    usbSerial.write((uint8_t*)message, 15);
  }

  // echo received data
  if (usbSerial.available()) {
    uint8_t buff[256];
    std::size_t len = 0;
    while (usbSerial.available() && len > 0) {
      len += usbSerial.read(&buff[len], 256 - len);
    }
    usbSerial.write(buff, len);
  }
}
