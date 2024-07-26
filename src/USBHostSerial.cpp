/*
Copyright (c) 2024 Bert Melis. All rights reserved.

This work is licensed under the terms of the MIT license.  
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.

Based on example code:
SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
SPDX-License-Identifier: CC0-1.0
*/

#include "USBHostSerial.h"

using namespace esp_usb;

USBHostSerial::USBHostSerial()
: _host_config{}
, _dev_config{}
, _line_coding{}
, _tx_buf_mem{}
, _rx_buf_mem{}
, _tx_buf_handle(nullptr)
, _rx_buf_handle(nullptr)
, _setupDone(false)
, _connected(false)
, _device_disconnected_sem(nullptr)
, _usb_lib_task_handle(nullptr) {
  _dev_config.connection_timeout_ms = 0;  // wait indefinitely for connection
  _dev_config.out_buffer_size = 512;
  _dev_config.in_buffer_size = 512;
  _dev_config.event_cb = _handle_event;
  _dev_config.data_cb = _handle_rx;
  _dev_config.user_arg = this;

  _tx_buf_handle = xRingbufferCreateStatic(USBHOSTSERIAL_BUFFERSIZE, RINGBUF_TYPE_BYTEBUF, _tx_buf_mem, &_tx_buf_data);
  _rx_buf_handle = xRingbufferCreateStatic(USBHOSTSERIAL_BUFFERSIZE, RINGBUF_TYPE_BYTEBUF, _rx_buf_mem, &_rx_buf_data);
  if (!_tx_buf_handle || !_rx_buf_handle) {
    abort();
  }
}

USBHostSerial::~USBHostSerial() {
  // TODO (bertmelis): implement destruction.
  vRingbufferDelete(_tx_buf_handle);
  vRingbufferDelete(_rx_buf_handle);
  abort();
}

USBHostSerial::operator bool() const {
  if (xSemaphoreTake(_device_disconnected_sem, 0) == pdTRUE) {
    xSemaphoreGive(_device_disconnected_sem);
    return false;
  }
  return true;
}

bool USBHostSerial::begin(int baud, int stopbits, int parity, int databits) {
  if (!_setupDone) {
    _setupDone = true;
    _setup();
  }

  _line_coding.dwDTERate = baud;
  _line_coding.bCharFormat = stopbits;
  _line_coding.bParityType = parity;
  _line_coding.bDataBits = databits;

  if (xTaskCreate(_USBHostSerial_task, "usb_dev_lib", 4096, this, 1, &_USBHostSerial_task_handle) == pdTRUE) {
    return true;
  }
  return false;
}

void USBHostSerial::end() {
  // TODO (bertmelis): implement end, together with destruction.
}

std::size_t USBHostSerial::write(uint8_t data) {
  if (xRingbufferSend(_tx_buf_handle, &data, 1, 10) == pdTRUE) {
    return 1;
  }
  return 0;
}

std::size_t USBHostSerial::write(uint8_t *data, std::size_t len) {
  std::size_t i = 0;
  for (; i < len; ++i) {
    if (write(data[i]) == 1) {
      ++i;
    } else {
      break;
    }
  }
  return i;
}

std::size_t USBHostSerial::available() {
  UBaseType_t numItemsWaiting;
  vRingbufferGetInfo(_rx_buf_handle, nullptr, nullptr, nullptr, nullptr, &numItemsWaiting);
  return numItemsWaiting;
}

uint8_t USBHostSerial::read() {
  std::size_t pxItemSize = 0;
  void* ret = xRingbufferReceiveUpTo(_rx_buf_handle, &pxItemSize, 0, 1);
  if (ret) {
    return *reinterpret_cast<uint8_t*>(ret);
  }
  return 0;
}

std::size_t USBHostSerial::read(uint8_t *dest, std::size_t size) {
  std::size_t retVal = 0;
  std::size_t pxItemSize = 0;
  while (size > pxItemSize) {
    void *ret = xRingbufferReceiveUpTo(_rx_buf_handle, &pxItemSize, 10, size - pxItemSize);
    if (ret) {
      memcpy(dest, ret, pxItemSize);
      retVal += pxItemSize;
      vRingbufferReturnItem(_rx_buf_handle, ret);
    } else {
      break;
    }
  }
  return retVal;
}

void USBHostSerial::_setup() {
  _device_disconnected_sem = xSemaphoreCreateBinary();
  assert(_device_disconnected_sem);

  // Install USB Host driver. Should only be called once in entire application
  _host_config.skip_phy_setup = false;
  _host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  ESP_ERROR_CHECK(usb_host_install(&_host_config));

  // Create a task that will handle USB library events
  BaseType_t task_created = xTaskCreate(_usb_lib_task, "usb_lib", 4096, this, 1, &_usb_lib_task_handle);
  assert(task_created == pdTRUE);

  ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

  // Register VCP drivers to VCP service
  VCP::register_driver<FT23x>();
  VCP::register_driver<CP210x>();
  VCP::register_driver<CH34x>();
}

bool USBHostSerial::_handle_rx(const uint8_t *data, size_t data_len, void *arg) {
  std::size_t lenReceived = 0;
  while (lenReceived < data_len) {
    if (xRingbufferSend(static_cast<USBHostSerial*>(arg)->_tx_buf_handle, &data[lenReceived], 1, 10) == pdTRUE) {
      ++lenReceived;
    } else {
      break;
    }
  }
  if (lenReceived < data_len) {
    // log overflow warning
  }
  return true;
}

void USBHostSerial::_handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx) {
  if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
    xSemaphoreGive(static_cast<USBHostSerial*>(user_ctx)->_device_disconnected_sem);
  }
}

void USBHostSerial::_usb_lib_task(void *arg) {
  while (1) {
    uint32_t event_flags;
    usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_ERROR_CHECK(usb_host_device_free_all());
    }
  }
}

void USBHostSerial::_USBHostSerial_task(void *arg) {
  USBHostSerial* thisInstance = static_cast<USBHostSerial*>(arg);
  while (1) {
    auto vcp = std::unique_ptr<CdcAcmDevice>(VCP::open(&(thisInstance->_dev_config)));
    vTaskDelay( 10 / portTICK_PERIOD_MS );
    ESP_ERROR_CHECK(vcp->line_coding_set(&(thisInstance->_line_coding)));

    while (1) {
      // check for data to send
      std::size_t pxItemSize = 0;
      void *data = xRingbufferReceiveUpTo(thisInstance->_tx_buf_handle, &pxItemSize, 10, USBHOSTSERIAL_BUFFERSIZE);
      if (data) {
        ESP_ERROR_CHECK(vcp->tx_blocking((uint8_t*)data, pxItemSize));
        vRingbufferReturnItem(thisInstance->_tx_buf_handle, data);
      }
      if (xSemaphoreTake(thisInstance->_device_disconnected_sem, 0) == pdTRUE) {
        break;
      }
      taskYIELD();
    }
  }
}
