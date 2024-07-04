/*
Copyright (c) 2024 Bert Melis. All rights reserved.

This work is licensed under the terms of the MIT license.  
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.
*/

#include "usb_host_serial.h"

usb_host_serial::usb_host_serial()
: _device_disconnected_sem(nullptr)
, _host_config{}
, _usb_lib_task_handle(nullptr)
, _dev_config{}
, _line_coding{} {
  _dev_config.connection_timeout_ms = 0;  // wait indefinitely for connection
  _dev_config.out_buffer_size = 512;
  _dev_config.in_buffer_size = 512;
  _dev_config.event_cb = _handle_event;
  _dev_config.data_cb = _handle_rx;
  _dev_config.user_arg = this;
}

usb_host_serial::~usb_host_serial() {
  // not supported
  abort();
}
explicit operator usb_host_serial::bool() const {
  // empty
}

bool usb_host_serial::begin(int baud, int stopbits, int parity, int databits) {
  if (!_device_disconnected_sem) {
    _setup();
  }

  _line_coding.dwDTERate = baud;
  _line_coding.bCharFormat = stopbits;
  _line_coding.bParityType = parity;
  _line_coding.bDataBits = databits;

  BaseType_t task_created = xTaskCreate(_usb_host_serial_task, "usb_dev_lib", 4096, this, 10, _usb_host_serial_task_handle);
  assert(task_created == pdTRUE);
}

void usb_host_serial::end() {
  // empty
}

std::size_t usb_host_serial::write(uint8_t data) {
  return 0;
}

std::size_t usb_host_serial::write(uint8_t *data, std::size_t len) {
  return 0;
}

std::size_t usb_host_serial::available() {
  return 0;
}

uint8_t usb_host_serial::read() {
  return 0;
}

std::size_t usb_host_serial::read(uint8_t *dest, std::size_t size) {
  return 0;
}

void usb_host_serial::_setup() {
  _device_disconnected_sem = xSemaphoreCreateBinary();
  assert(_device_disconnected_sem);

  // Install USB Host driver. Should only be called once in entire application
  _host_config.skip_phy_setup = false;
  _host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  ESP_ERROR_CHECK(usb_host_install(&_host_config));

  // Create a task that will handle USB library events
  BaseType_t task_created = xTaskCreate(usb_lib_task, "usb_lib", 4096, this, 10, _usb_lib_task_handle);
  assert(task_created == pdTRUE);

  ESP_LOGI(TAG, "Installing CDC-ACM driver");
  ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

  // Register VCP drivers to VCP service
  VCP::register_driver<FT23x>();
  VCP::register_driver<CP210x>();
  VCP::register_driver<CH34x>();
}

bool usb_host_serial::_handle_rx(const uint8_t *data, size_t data_len, void *arg) {
  // data received
  return true;
}

void usb_host_serial::_handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx) {
  if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
    xSemaphoreGive(reinterpret_cast<usb_host_serial*>(user_ctx)->_device_disconnected_sem);
  }
}

void usb_host_serial::_usb_lib_task(void *arg) {
  while (1) {
    uint32_t event_flags;
    usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_ERROR_CHECK(usb_host_device_free_all());
    }
  }
}

void usb_host_serial::_usb_host_serial_task(void *arg) {
  while (1) {
    auto vcp = std::unique_ptr<CdcAcmDevice>(VCP::open(&(reinterpret_cast<usb_host_serial*>(arg)->_dev_config)));
    delay(10);
    ESP_ERROR_CHECK(vcp->line_coding_set(&(reinterpret_cast<usb_host_serial*>(arg)->_line_coding)));

    while (1) {
      // check for data to send
      //ESP_ERROR_CHECK(vcp->tx_blocking(data, sizeof(data)));
      //ESP_ERROR_CHECK(vcp->set_control_line_state(true, true));
      if (xSemaphoreTake(reinterpret_cast<usb_host_serial*>(arg)->_device_disconnected_sem, 0) == pdTrue) {
        break;
      }
    }
  }
}