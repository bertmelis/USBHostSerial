/*
Copyright (c) 2024 Bert Melis. All rights reserved.

This work is licensed under the terms of the MIT license.  
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.

Based on example code:
SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
SPDX-License-Identifier: CC0-1.0
*/

#pragma once

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"

#include <usb/cdc_acm_host.h>
#include <usb/vcp_ch34x.hpp>
#include <usb/vcp_cp210x.hpp>
#include <usb/vcp_ftdi.hpp>
#include <usb/vcp.hpp>
#include <usb/usb_host.h>

#ifndef USBHOSTSERIAL_BUFFERSIZE
  #define USBHOSTSERIAL_BUFFERSIZE 256
#endif

class USBHostSerial {
 public:
  USBHostSerial();
  ~USBHostSerial();

  // true if serial-over-usb device is available eg. a device is connected
  explicit operator bool() const;

  /*
  baudrate
  stopbits: 0: 1 stopbit, 1: 1.5 stopbits, 2: 2 stopbits
  parity: 0: None, 1: Odd, 2: Even, 3: Mark, 4: Space
  databits: 8
  */
  bool begin(int baud, int stopbits, int parity, int databits);

  // not yet implemented
  void end();

  // write one byte to serial-over-usb. returns 0 when buffer is full or device is not available
  std::size_t write(uint8_t data);

  // write data to serial-over-usb. returns length of data that was actually written: 0 when buffer is full or device is not available
  std::size_t write(uint8_t *data, std::size_t len);

  // get size of available RX data
  std::size_t available();

  // read one byte from available data. If no data is available, 0 is returned (check with `available()`)
  uint8_t read();

  // read available data into `dest`. returns number of bytes written. maximum number of `size` bytes will be written
  std::size_t read(uint8_t *dest, std::size_t size);

 protected:
  usb_host_config_t _host_config;
  cdc_acm_host_device_config_t _dev_config;
  cdc_acm_line_coding_t _line_coding;
  uint8_t _tx_buf_mem[USBHOSTSERIAL_BUFFERSIZE];
  uint8_t _rx_buf_mem[USBHOSTSERIAL_BUFFERSIZE];
  RingbufHandle_t _tx_buf_handle;
  StaticRingbuffer_t _tx_buf_data;
  RingbufHandle_t _rx_buf_handle;
  StaticRingbuffer_t _rx_buf_data;
  bool _setupDone;
  bool _connected;

 private:
  void _setup();
  static bool _handle_rx(const uint8_t *data, size_t data_len, void *arg);
  static void _handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx);
  static void _usb_lib_task(void *arg);
  static void _usb_host_serial_task(void *arg);

  SemaphoreHandle_t _device_disconnected_sem;
  
  TaskHandle_t _usb_lib_task_handle;
  TaskHandle_t _usb_host_serial_task_handle;
};
