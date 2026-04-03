# OpenThread Border Router Example with Standalone RCP

## Overview

This example is similar to the [ot_br example](https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_br) in ESP-IDF, but supports additional features such as RCP update upon boot.

## How to use example

Refer to the main `README.md` for detailed instructions on building, flashing and running this example.

For standalone RCP modules, the following configuration options should be selected (refer to `sdkconfig.defaults.esp32c5`):

```
CONFIG_ESP_BR_BOARD_STANDALONE=y
CONFIG_ESP_CONSOLE_UART_DEFAULT=y (if you are using the UART USB-C port on the Main Processor DevKit)
CONFIG_ESP_BR_C6_TARGET=y (if you are using an ESP32-C6 as RCP)
```

### Hardware Required

> **Note:** The native 15.4 radio on the main processor is not used because `CONFIG_OPENTHREAD_RADIO_SPINEL_UART=y` is selected in `sdkconfig.defaults`. This is intended behavior because there is only one antenna on the ESP32-C5, and Wi-Fi and thread cannot receive packets at the same time. Using a single chip to handle both Wi-Fi and Thread interfaces is not recommended.

You can use either the ESP32-H2 (default) or ESP32-C6 as an Radio Co-Processor (RCP).

#### Connect the main processor to an RCP using UART (recommended):
Main Processor pin  | RCP (H2 or C6) pin
--------------------|-------------------
  GND               |     G
  5V                |     5V
  GPIO4  (UART RX)  |     TX
  GPIO5  (UART TX)  |     RX
  GPIO7             |     RST
  GPIO8             |     GPIO9 (BOOT)

#### Connect the main processor to an RCP using SPI:
Main Processor pin  | RCP (H2 or C6) pin
--------------------|-------------------
  GND               |     G
  GPIO7             |     RST
  GPIO8  (SPI INTR) |     GPIO9 (BOOT)
  GPIO20 (SPI CS)   |     GPIO2
  GPIO21 (SPI MOSI) |     GPIO3
  GPIO22 (SPI CLK)  |     GPIO0
  GPIO23 (SPI MISO) |     GPIO1

### Notes

1. Please check the GPIO pin configuration (`radio_uart_config`) in `esp_ot_config.h` for both the ot_rcp and thread_br examples to accurately reflect the GPIO connections between the Wi-Fi and 802.15.4 SoCs.
