# USBHostSerial

:construction: **Work in progress.** :construction:

The goal is to create an "Arduino Serial"-like interface for USB-to-UART devices, connected to your ESP32-S3 which acts as USB-host.

Based on the [virtual com port example from Espressif](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/host/cdc/cdc_acm_vcp).

## Dependencies

The following dependencies are already included in the library. There is no need to install them separately.

- [USB Host CDC-ACM Class Drive](https://components.espressif.com/components/espressif/usb_host_cdc_acm)
- [Virtual COM Port Service](https://components.espressif.com/components/espressif/usb_host_vcp)
- Device drivers:
  - [CH34x USB-UART converter driver](https://components.espressif.com/components/espressif/usb_host_ch34x_vcp)
  - [Silicon Labs CP210x USB-UART converter driver](https://components.espressif.com/components/espressif/usb_host_cp210x_vcp)
  - [FTDI UART-USB converters driver](https://components.espressif.com/components/espressif/usb_host_ftdi_vcp)

The library can be used in Arduino IDE and in [pioarduino](https://github.com/pioarduino).

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
