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
, _line_coding{}
, _tx_buf_mem{}
, _tx_buf_handle(nullptr)
, _tx_buf_data{}
, _rx_buf_mem{}
, _rx_buf_handle(nullptr)
, _rx_buf_data{}
, _setupDone(false)
, _connected(false)
, _device_disconnected_sem(nullptr)
, _usb_lib_task_handle(nullptr)
, _logger(nullptr) {
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
  return _connected;
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
    _log("USB setup done");
    return true;
  }
  _log("USB setup failed");
  return false;
}

void USBHostSerial::end() {
  // TODO (bertmelis): implement end, together with destruction.
}

std::size_t USBHostSerial::write(uint8_t data) {
  if (xRingbufferSend(_tx_buf_handle, &data, 1, pdMS_TO_TICKS(1)) == pdTRUE) {
    _log("USB buf write: 1");
    return 1;
  }
  _log("USB buf write: 0");
  return 0;
}

std::size_t USBHostSerial::write(const uint8_t *data, std::size_t len) {
  UBaseType_t numItemsWaiting;
  vRingbufferGetInfo(_tx_buf_handle, nullptr, nullptr, nullptr, nullptr, &numItemsWaiting);
  std::size_t maxSize = USBHOSTSERIAL_BUFFERSIZE - numItemsWaiting;
  if (maxSize < len) {
    char buf[40];
    snprintf(buf, 40, "USB buf overflow: %u-%u", len, maxSize);
    _log(buf);
  } else {
    if (xRingbufferSend(_tx_buf_handle, data, len, pdMS_TO_TICKS(1)) == pdTRUE) {
      char buf[30];
      snprintf(buf, 30, "USB buf write: %u", len);
      _log(buf);
      return len;
    }
  }
  return 0;
}

std::size_t USBHostSerial::available() {
  UBaseType_t numItemsWaiting;
  vRingbufferGetInfo(_rx_buf_handle, nullptr, nullptr, nullptr, nullptr, &numItemsWaiting);
  return numItemsWaiting;
}

uint8_t USBHostSerial::read() {
  std::size_t pxItemSize = 0;
  uint8_t retVal = 0;
  void* ret = xRingbufferReceiveUpTo(_rx_buf_handle, &pxItemSize, pdMS_TO_TICKS(1), 1);
  if (pxItemSize > 0) {
    retVal = *reinterpret_cast<uint8_t*>(ret);
    vRingbufferReturnItem(_rx_buf_handle, ret);
  }
  _log("USB buf read: 1");
  return retVal;
}

std::size_t USBHostSerial::read(uint8_t *dest, std::size_t size) {
  std::size_t retVal = 0;
  std::size_t pxItemSize = 0;
  while (size > pxItemSize) {
    void *ret = xRingbufferReceiveUpTo(_rx_buf_handle, &pxItemSize, pdMS_TO_TICKS(1), size - pxItemSize);
    if (ret) {
      std::memcpy(dest, ret, pxItemSize);
      retVal += pxItemSize;
      vRingbufferReturnItem(_rx_buf_handle, ret);
    } else {
      break;
    }
  }
  char buf[30];
  snprintf(buf, 30, "USB buf read: %u", retVal);
  _log(buf);
  return retVal;
}

void USBHostSerial::setLogger(USBHostSerialLoggerFunc logger) {
  _logger = logger;
}

void USBHostSerial::_setup() {
  _device_disconnected_sem = xSemaphoreCreateBinary();
  assert(_device_disconnected_sem);
  xSemaphoreGive(_device_disconnected_sem);

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
  while (lenReceived < data_len && xRingbufferSend(static_cast<USBHostSerial*>(arg)->_rx_buf_handle, &data[lenReceived], 1, pdMS_TO_TICKS(10)) == pdTRUE) {
    ++lenReceived;
  }
  if (lenReceived < data_len) {
    // log overflow warning
    static_cast<USBHostSerial*>(arg)->_log("USB rx buf overflow");
  }
  char buf[30];
  snprintf(buf, 30, "USB rx: %u", lenReceived);
  static_cast<USBHostSerial*>(arg)->_log(buf);
  return true;
}

void USBHostSerial::_handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx) {
  if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
    xSemaphoreGive(static_cast<USBHostSerial*>(user_ctx)->_device_disconnected_sem);
    static_cast<USBHostSerial*>(user_ctx)->_connected = false;
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
    // try to open USB VCP device
    const cdc_acm_host_device_config_t dev_config = {
      .connection_timeout_ms = 10,
      .out_buffer_size = USBHOSTSERIAL_BUFFERSIZE,
      .in_buffer_size = USBHOSTSERIAL_BUFFERSIZE,
      .event_cb = _handle_event,
      .data_cb = _handle_rx,
      .user_arg = thisInstance,
    };
    vTaskDelay(pdMS_TO_TICKS(10));
    auto vcp = std::unique_ptr<CdcAcmDevice>(VCP::open(&dev_config));
    vTaskDelay(pdMS_TO_TICKS(10));
    if (vcp == nullptr) {
      continue;
    } else {
      thisInstance->_log("USB CDC device opened");
    }

    // Mark connected and configure
    thisInstance->_connected = true;
    xSemaphoreTake(thisInstance->_device_disconnected_sem, portMAX_DELAY);
    if (vcp->line_coding_set(&(thisInstance->_line_coding)) == ESP_OK) {
      thisInstance->_log("USB line coding set");
    } else {
      thisInstance->_log("USB line coding error");
      continue;
    }

    while (1) {
      // check if still connected
      if (xSemaphoreTake(thisInstance->_device_disconnected_sem, 0) == pdTRUE) {
        thisInstance->_connected = false;
        break;
      }

      // check for data to send
      std::size_t pxItemSize = 0;
      uint8_t *data = (uint8_t*)xRingbufferReceiveUpTo(thisInstance->_tx_buf_handle, &pxItemSize, pdMS_TO_TICKS(10), USBHOSTSERIAL_BUFFERSIZE);
      if (data && thisInstance->_connected) {
        char buf[30];
        snprintf(buf, 30, "USB tx: %c...(%u)", data[0], pxItemSize);
        thisInstance->_log(buf);
        ESP_ERROR_CHECK(vcp->tx_blocking(data, pxItemSize, 1000));
        vRingbufferReturnItem(thisInstance->_tx_buf_handle, (void*)data);
      }
      taskYIELD();
    }
  }
}

void USBHostSerial::_log(const char* msg) {
  if (_logger) {
    _logger(msg);
  }
}
