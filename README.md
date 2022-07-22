# esp-thread-br

esp-thread-br is the official [ESP Thread Border Router](https://openthread.io/guides/border-router/espressif-esp32) SDK. It supports all fundamental network features to build a Thread Border Router and integrates rich product level features for quick productization.

## Software Components

![esp_br_solution](docs/images/esp-thread-border-router-solution.png)

Currently the OpenThread port and ESP Border Router core implementation is provided as pre-built library in ESP-IDF.

## Hardware Platforms

The ESP Thread Border Router consists of two SoCs:

* An ESP32 series Wi-Fi SoC (ESP32, ESP32-C, ESP32-S, etc) loaded with ESP Thread Border Router and OpenThread Stack.
* An ESP32-H 802.15.4 SoC loaded with OpenThread RCP.

### ESP Thread Border Router Board

The ESP Thread border router board provides an integrated module of an ESP32-S3 SoC and an ESP32-H2 RCP.

![br_dev_kit](docs/images/esp-thread-border-router-devkit.png)

The two SoCs are connected with following interfaces:
* UART and SPI for serial communication
* RESET and BOOT pins for RCP Update
* 3-Wires PTA for RF coexistence

### Standalone Modules

The SDK also supports manually connecting an ESP32-H2 RCP to an ESP32 series Wi-Fi SoC.

ESP32 pin | ESP32-H2 pin
----------|-------------
  GND     |      G
  GPIO17  |      TX
  GPIO18  |      RX
  GPIO4   |      RST
  GPIO5   |  GPIO9 (BOOT)

The following image shows an example connection between ES32-H2 and ESP32:

![br_standalone](docs/images/thread-border-router-esp32-esp32h2.jpg)

In this setup, only UART interface is connected, so it doesn't support RCP Update or RF Coexistence features. You can refer to [ot_br](https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_br) example in esp-idf as a quick start.

## Provided Features

These features are currently provided by the SDK:

* **Bi-directional IPv6 Connectivity**: The devices on the backbone link (typically Wi-Fi) and the Thread network can reach each other.
* **Service Discovery Delegate**: The nodes on the Thread network can find the mDNS services on the backbone link.
* **Service Registration Server**: The nodes on the Thread network can register services to the border router for devices on the backbone link to discover.
* **Multicast Forwarding**: The devices joining the same multicast group on the backbone link and the Thread network can be reached with one single multicast.
* **RCP Update**: The built border router image will contain an updatable RCP image and can automatically update the RCP on version mismatch or RCP failure.
* **Web GUI**: The border router will enable a web server and provide some practical functions including Thread network discovery, network formation and status query. 

In the future releases, the following features will be added:

* SPI Communication
* RF Coexistence
* NAT64
