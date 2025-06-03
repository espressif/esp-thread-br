# OpenThread Border Router Example for ESP32-P4

## Overview

There are 2 ways to run this example on the ESP32-P4:
- [(Default): P4 main processor (Ethernet) + C6 Thread radio co-processor](#p4-main-processor-ethernet--c6-thread-radio-co-processor)
- [(Wi-fi): P4 main processor + C6 Wi-Fi co-processor + External H2 Thread radio co-processor](#p4-main-processor--c6-wi-fi-co-processor--external-h2-thread-radio-co-processor)

## How to use example

### Hardware Required

The [ESP32-P4-Function-EV-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) is recommended for this example. If you wish to use standalone modules, you may refer to the [OpenThread Border Router Example](https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_br) in esp-idf.

### Set up ESP IDF

Refer to [ESP-IDF Get Started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/index.html).

Currently, it is recommended to use ESP-IDF [v5.4.2](https://github.com/espressif/esp-idf/tree/v5.4.2) release.

### P4 main processor (Ethernet) + C6 Thread radio co-processor

#### Configure the project

The default configurations in `sdkconfig.defaults.esp32p4` will be applied by using the command:

```
idf.py set-target esp32p4
```

#### Create the RCP firmware image

First build the [ot_rcp](https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_rcp) example in IDF **using the ESP32C6 target**. In the building process, the built RCP image will be automatically packed into the border router firmware.

> ⚠️ **Warning:** Using the default configurations for this example will flash the built-in C6 co-processor with the OT_RCP firmware, replacing the original ESP-Hosted slave firmware. If you wish to use this example with C6 running Wi-Fi, please refer to the [instructions for Wi-Fi](#configure-the-project-wi-fi).

The border router supports updating the C6 RCP upon boot, with the following header pin connections:

ESP32-P4 pin        | ESP32-C6 pin
--------------------|-------------
  GPIO4  (UART RX)  |     TX0
  GPIO5  (UART TX)  |     RX0
  GPIO7             |     EN
  GPIO8             |     BOOT

#### Build, Flash, and Run

> The `OPENTHREAD_BR_AUTO_START` option is enabled by default, you may choose to configure the `Thread Operational Dataset` options in menuconfig before building.

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT build erase-flash flash monitor
```

### P4 main processor + C6 Wi-Fi co-processor + External H2 Thread radio co-processor

#### Configure the project

Add `esp_wifi_remote` and `esp_hosted` components to the project:

```
idf.py add-dependency "espressif/esp_wifi_remote"
idf.py add-dependency "espressif/esp_hosted"
```

The default configurations in `sdkconfig.defaults.esp32p4` will be applied by using the command:

```
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.wifi.esp32p4" set-target esp32p4
```

#### Connect an ESP32-H2 RCP to an ESP32-P4 using UART:
ESP32-P4 pin        | ESP32-H2 pin
--------------------|-------------
  GND               |     G
  5V                |     5V
  GPIO4  (UART RX)  |     TX
  GPIO5  (UART TX)  |     RX
  GPIO7             |     RST
  GPIO8             |     GPIO9 (BOOT)

#### Flashing ESP32-C6 Wi-Fi Module (optional)

The ESP32-C6 module in the ESP32-P4-Function-EV-Board is pre-flashed with ESP-Hosted slave firmware, so there is no need to flash any additional firmware to the ESP32-C6. However, you may refer to [these instructions for flashing ESP32-C6](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/esp32_p4_function_ev_board.md#5-flashing-esp32-c6) for more information if you wish to update to a newer version of ESP-Hosted slave firmware.

#### Build, Flash, and Run

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT build erase-flash flash monitor
```
