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

USBHostSerial::USBHostSerial(uint16_t vid, uint16_t pid)
: _host_config{}
, _line_coding{}
, _tx_buf_mem{}
, _tx_buf_handle(nullptr)
, _tx_buf_data{}
, _rx_buf_mem{}
, _rx_buf_handle(nullptr)
, _rx_buf_data{}
, _setupDone(false)
, _fallback(false)
, _vid(vid)
, _pid(pid)
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
    return 1;
  }
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
  return retVal;
}

std::size_t USBHostSerial::read(uint8_t *dest, std::size_t size) {
  std::size_t retVal = 0;
  std::size_t pxItemSize = 0;
  while (size > retVal) {
    std::size_t remaining = size - retVal;
    if(remaining == 0){
      break;
    }

    void *ret = xRingbufferReceiveUpTo(_rx_buf_handle, &pxItemSize, pdMS_TO_TICKS(1), remaining);

    if(!ret || pxItemSize == 0){
      break;
    }

    std::memcpy(dest + retVal, ret, pxItemSize);
    retVal += pxItemSize;
    vRingbufferReturnItem(_rx_buf_handle, ret);
  }
  return retVal;
}

void USBHostSerial::setLogger(USBHostSerialLoggerFunc logger) {
  _logger = logger;
}

void USBHostSerial::_setup() {
  _device_disconnected_sem = xSemaphoreCreateBinary();
  assert(_device_disconnected_sem);
  xSemaphoreGive(_device_disconnected_sem);  // make available for first use

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
  esp_err_t err = ESP_OK;  // reusable
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
    cdc_acm_dev_hdl_t cdc_dev = NULL;
    auto vcp = std::unique_ptr<CdcAcmDevice>(VCP::open(&dev_config));
    if (vcp == nullptr) {
      // try to fallback to CDC
      err = cdc_acm_host_open(thisInstance->_vid, thisInstance->_pid, 0, &dev_config, &cdc_dev);
      if (err != ESP_OK) {
        continue;
      }
      thisInstance->_fallback = true;
      thisInstance->_log("USB CDC device opened");
    } else {
      thisInstance->_fallback = false;
      thisInstance->_log("USB VCP device opened");
    }

    // mark connected
    xSemaphoreTake(thisInstance->_device_disconnected_sem, portMAX_DELAY);

    // set line coding
    err = ESP_OK;
    if (thisInstance->_fallback) {
      err = cdc_acm_host_line_coding_get(cdc_dev, &(thisInstance->_line_coding));
    } else {
      err = vcp->line_coding_set(&(thisInstance->_line_coding));
    }
    if (err == ESP_OK) {
      thisInstance->_log("USB line coding set");
    } else {
      thisInstance->_log("USB line coding error");
      continue;
    }

    // all set, enter loop to start sending
    while (1) {
      // check if still connected
      if (xSemaphoreTake(thisInstance->_device_disconnected_sem, 0) == pdTRUE) {
        break;
      }

      // check for data to send
      std::size_t pxItemSize = 0;
      void *data = xRingbufferReceiveUpTo(thisInstance->_tx_buf_handle, &pxItemSize, pdMS_TO_TICKS(10), USBHOSTSERIAL_BUFFERSIZE);
      if (data) {
        if (thisInstance->_fallback) {
          err = cdc_acm_host_data_tx_blocking(cdc_dev, (uint8_t*)data, pxItemSize, 1000);
        } else {
          err = vcp->tx_blocking((uint8_t*)data, pxItemSize, 1000);
        }
        if (err == ESP_OK) {
          vRingbufferReturnItem(thisInstance->_tx_buf_handle, data);
        } else {
          thisInstance->_log("Error writing to USB");
        }
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
