/*
Copyright (c) 2024 Bert Melis. All rights reserved.

This work is licensed under the terms of the MIT license.  
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.
*/

#include "usb_host_serial.h"

using namespace esp_usb;

usb_host_serial::usb_host_serial()
: _host_config{}
, _dev_config{}
, _line_coding{}
, _tx_buf_mem{}
, _rx_buf_mem{}
, _tx_buf_handle(nullptr)
, _rx_buf_handle(nullptr)
, _setupDone(false)
, _device_disconnected_sem(nullptr)
, _usb_lib_task_handle(nullptr)
, _usb_host_serial_task_handle(nullptr) {
  _dev_config.connection_timeout_ms = 0;  // wait indefinitely for connection
  _dev_config.out_buffer_size = USB_HOST_SERIAL_BUFFERSIZE;
  _dev_config.in_buffer_size = USB_HOST_SERIAL_BUFFERSIZE;
  _dev_config.event_cb = _handle_event;
  _dev_config.data_cb = _handle_rx;
  _dev_config.user_arg = this;

  _tx_buf_handle = xRingbufferCreateStatic(USB_HOST_SERIAL_BUFFERSIZE, RINGBUF_TYPE_BYTEBUF, _tx_buf_mem, &_tx_buf_data);
  _rx_buf_handle = xRingbufferCreateStatic(USB_HOST_SERIAL_BUFFERSIZE, RINGBUF_TYPE_BYTEBUF, _rx_buf_mem, &_rx_buf_data);
  if (!_tx_buf_handle || !_rx_buf_handle) {
    abort();
  }
}

usb_host_serial::~usb_host_serial() {
  // not supported
  vRingbufferDelete(_tx_buf_handle);
  vRingbufferDelete(_rx_buf_handle);
  abort();
}

usb_host_serial:: operator bool() const {
  if (xSemaphoreTake(_device_disconnected_sem, 10) == pdTRUE) {
    xSemaphoreGive(_device_disconnected_sem);
    return false;
  }
  return true;
}

bool usb_host_serial::begin(int baud, int stopbits, int parity, int databits) {
  if (!_setupDone) {
    _setupDone = true;
    _setup();
  }

  _line_coding.dwDTERate = baud;
  _line_coding.bCharFormat = stopbits;
  _line_coding.bParityType = parity;
  _line_coding.bDataBits = databits;

  if (xTaskCreate(_usb_host_serial_task, "usb_dev_lib", 4096, this, 1, &_usb_host_serial_task_handle) == pdTRUE) {
    return true;
  }
  return false;
}

void usb_host_serial::end() {
  // empty
}

std::size_t usb_host_serial::write(uint8_t data) {
  if (xRingbufferSend(_tx_buf_handle, &data, 1, 10) == pdTRUE) {
    return 1;
  }
  return 0;
}

std::size_t usb_host_serial::write(uint8_t *data, std::size_t len) {
  std::size_t i = 0;
  for (; i < len; ++i) {
    if (write(data[i]) != 1) {
      break;
    }
  }
  return i;
}

std::size_t usb_host_serial::available() {
  std::size_t itemsWaiting;
  vRingbufferGetInfo(_rx_buf_handle, nullptr, nullptr, nullptr, nullptr, &itemsWaiting);
  return itemsWaiting;
}

uint8_t usb_host_serial::read() {
  uint8_t retVal = 0;
  std::size_t pxItemSize = 0;
  void* ret = xRingbufferReceiveUpTo(_rx_buf_handle, &pxItemSize, 10, 1);
  if (ret) {
    retVal = *static_cast<uint8_t*>(ret);
    vRingbufferReturnItem(_rx_buf_handle, ret);
  }
  return retVal;
}

std::size_t usb_host_serial::read(uint8_t *dest, std::size_t size) {
  uint8_t retVal = 0;
  std::size_t pxItemSize = 0;
  while (size > pxItemSize) {
    void *ret = xRingbufferReceiveUpTo(_rx_buf_handle, &pxItemSize, 10, size - pxItemSize);
    if (ret) {
      std::memcpy(dest, ret, pxItemSize);
      retVal += pxItemSize;
      vRingbufferReturnItem(_rx_buf_handle, ret);
    } else {
      break;
    }
  }
  return retVal;
}

void usb_host_serial::_setup() {
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

bool usb_host_serial::_handle_rx(const uint8_t *data, size_t data_len, void *arg) {
  std::size_t lenReceived = 0;
  while (lenReceived < data_len) {
    if (xRingbufferSend(static_cast<usb_host_serial*>(arg)->_tx_buf_handle, &data[lenReceived], 1, 10) == pdFALSE) {
      break;
    } else {
      ++lenReceived;
    }
  }
  if (lenReceived < data_len) {
    // log overflow warning
  }
  return true;
}

void usb_host_serial::_handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx) {
  if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
    xSemaphoreGive(static_cast<usb_host_serial*>(user_ctx)->_device_disconnected_sem);
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
  usb_host_serial* thisInstance = static_cast<usb_host_serial*>(arg);
  while (1) {
    auto vcp = std::unique_ptr<CdcAcmDevice>(VCP::open(&thisInstance->_dev_config));
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(vcp->line_coding_set(&thisInstance->_line_coding));

    while (1) {
      // check for data to send
      std::size_t pxItemSize = 0;
      uint8_t *data = static_cast<uint8_t*>(xRingbufferReceiveUpTo(thisInstance->_tx_buf_handle, &pxItemSize, 10, USB_HOST_SERIAL_BUFFERSIZE));
      if (data) {
        ESP_ERROR_CHECK(vcp->tx_blocking(data, pxItemSize));
        vRingbufferReturnItem(thisInstance->_tx_buf_handle, data);
        ESP_ERROR_CHECK(vcp->set_control_line_state(true, true));
      }
      if (xSemaphoreTake(thisInstance->_device_disconnected_sem, 0) == pdTRUE) {
        break;
      }
    }
  }
}
