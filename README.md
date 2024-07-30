# USBHostSerial

:construction: **Work in progress.** :construction:

The goal is to create an "Arduino Serial"-like interface for USB-to-UART devices, connected to your ESP32-S3 which acts as USB-host.

Based on the [virtual com port example from Espressif](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/host/cdc/cdc_acm_vcp).

## Dependencies

The following dependencies have to be installed:

- [Espressif TinyUSB fork](https://components.espressif.com/components/espressif/tinyusb)
- [Espressif's additions to TinyUSB](https://components.espressif.com/components/espressif/esp_tinyusb)
- [USB Host CDC-ACM Class Drive](https://components.espressif.com/components/espressif/usb_host_cdc_acm/versions/2.0.3)
- [Virtual COM Port Service](https://components.espressif.com/components/espressif/usb_host_vcp)
- Device drivers, depending in your application:
  - [CH34x USB-UART converter driver](https://components.espressif.com/components/espressif/usb_host_ch34x_vcp/versions/2.0.0)
  - [Silicon Labs CP210x USB-UART converter driver](https://components.espressif.com/components/espressif/usb_host_cp210x_vcp/versions/2.0.0)
  - [FTDI UART-USB converters driver](https://components.espressif.com/components/espressif/usb_host_ftdi_vcp/versions/2.0.0)



## License

The original example code is covered by this copyright notice:

```
/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
```

Any modifications deemed "substantial" in this repo are covered by the included MIT license.
