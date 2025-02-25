# ESP Thread Border Router SDK

ESP-THREAD-BR is the official Espressif Thread Border Router SDK. It supports all fundamental network features to build a [Thread Border Router](https://openthread.io/guides/border-router) (BR) and integrates rich product level features for quick productization.

# Software Components

![esp_br_solution](docs/images/esp-thread-border-router-solution.png)

The SDK is built on top of [ESP-IDF](https://github.com/espressif/esp-idf) and [OpenThread](https://github.com/openthread/openthread). The BR implementation is provided as pre-built library in ESP-IDF.

# Hardware Platforms

## Wi-Fi based Thread Border Router

The Wi-Fi based ESP Thread BR consists of two SoCs:

* An ESP32 series Wi-Fi SoC (ESP32, ESP32-C, ESP32-S, etc) loaded with ESP Thread BR and OpenThread Stack.
* An ESP32-H 802.15.4 SoC loaded with OpenThread RCP.

### ESP Thread Border Router Board

The ESP Thread border router board provides an integrated module of an ESP32-S3 SoC and an ESP32-H2 RCP.

![br_dev_kit](docs/images/esp-thread-border-router-board.png)

The two SoCs are connected with following interfaces:
* UART and SPI for serial communication
* RESET and BOOT pins for RCP Update
* 3-Wires PTA for RF coexistence

### Standalone Modules

The SDK also supports manually connecting an ESP32-H2 RCP to an ESP32 series Wi-Fi SoC.

For standalone modules, we recommend the [ot_br](https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_br) example in esp-idf as a quick start.

#### Connect an ESP32-H2 RCP to an ESP32 series Wi-Fi SoC using UART:
ESP32 pin           | ESP32-H2 pin
--------------------|-------------
  GND               |     G
  GPIO17 (UART RX)  |     TX
  GPIO18 (UART TX)  |     RX
  GPIO7             |     RST
  GPIO8             |     GPIO9 (BOOT)

#### Connect an ESP32-H2 RCP to an ESP32 series Wi-Fi SoC using SPI:
ESP32 pin           | ESP32-H2 pin
--------------------|-------------
  GND               |     G
  GPIO7             |     RST
  GPIO8  (SPI INTR) |     GPIO9 (BOOT)
  GPIO10 (SPI CS)   |     GPIO2
  GPIO11 (SPI MOSI) |     GPIO3
  GPIO12 (SPI CLK)  |     GPIO0
  GPIO13 (SPI MISO) |     GPIO1

Note that:
1. Please update the GPIO pin configuration (`radio_uart_config`) in `esp_ot_config.h` for both the ot_rcp and ot_br examples to accurately reflect the GPIO connections between the Wi-Fi and 802.15.4 SoCs.

2. The configure `ESP_CONSOLE_USB_SERIAL_JTAG` is enabled by default, please connect the USB port of the ESP32 series Wi-Fi SoC to host.

## Ethernet based Thread Border Router

Similar to the previous Wi-Fi based Thread BR setup, but a device with Ethernet interface is required, such as [ESP32-Ethernet-Kit](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-ethernet-kit.html).

# Provided Features

These features are currently provided by the SDK:

* **Bi-directional IPv6 Connectivity**: The devices on the backbone link (typically Wi-Fi or Ethernet) and the Thread network can reach each other.
* **Service Discovery Delegate**: The devices on the Thread network can find the mDNS services on the backbone link.
* **Service Registration Server**: The devices on the Thread network can register services to the BR for devices on the backbone link to discover.
* **Multicast Forwarding**: The devices joining the same multicast group on the backbone link and the Thread network can be reached with one single multicast.
* **NAT64**: The devices can access the IPv4 Internet via the BR.
* **Credential Sharing**: The BR could safely share administrative access and allow extracting the network credentials of the network.
* **TREL**: It enables Thread devices to communicate directly over IPv6-based links other than IEEE 802.15.4, including Wi-Fi and Ethernet.
* **RCP Update**: The built BR image will contain an updatable RCP image and can automatically update the RCP on version mismatch or RCP failure.
* **Web GUI**: The BR will enable a web server and provide some practical functions including Thread network discovery, network formation, status query and topology monitor.
* **RF Coexistence**: The BR supports optional external coexistence, a feature that enhances the transmission performance when there are channel conflicts between the Wi-Fi and Thread networks.

# Resources

* Documentation for the latest version: https://docs.espressif.com/projects/esp-thread-br/. This documentation is built from the [docs directory](docs) of this repository.

* [Check the Issues section on github](https://github.com/espressif/esp-thread-br/issues) if you find a bug or have a feature request. Please check existing Issues before opening a new one.

* If you're interested in contributing to ESP-THREAD-BR, please check the [Contributions Guide](https://docs.espressif.com/projects/esp-idf/en/latest/contribute/index.html).
