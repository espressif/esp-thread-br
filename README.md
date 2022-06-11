# esp-thread-br

esp-thread-br is the official [ESP Thread Border Router](https://openthread.io/guides/border-router/espressif-esp32) SDK. It supports all fundamental network features to build a Thread Border Router and integrates rich product level features for quick productization.

## Software components

The ESP Thread Border Router is composed of the following components:

* OpenThread SDK
* OpenThread port library for ESP32 series.
* Border router library for ESP32 series.
* Extra components in the ESP Thread SDK

Currently the port library and the border router library are provided as pre-built library in ESP-IDF.

## Hardware platforms

### ESP Thread Border Router Dev kit

The Thread border router dev kit provides an integrated module of an ESP32-S3 SoC and an ESP32-H2 RCP.

![br_dev_kit](docs/images/ESP-Thread-Border-Router-Front.jpg)

When using the SDK with the dev kit, the following menuconfig options must be enabled:

* `CONFIG_BR_BOARD_DEV_KIT`
* `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`

### Standlone SoCs

The SDK also supports manually connecting an ESP32-H2 RCP to an ESP32 series Wi-Fi SoC.

The following image shows an example connection between ES32-H2 and ESP32.

![br_standalone](docs/images/thread-border-router-esp32-esp32h2.jpg)


## Provided features


These features are currently provided in the repository:

* Bi-directional IPv6 connectivity: The devices on the backbone link (typically Wi-Fi) and the Thread network can reach each other.
* Service discovery delegate: The nodes on the Thread network can find the mDNS services on the backbone link.
* Serveice registration server: The nodes on the Thread network can register services to the border router for devices on the backbone link to discover.
* Muliticast forwarding: The devices joining the same multicast group on the backbone link and the Thread network can be reached with one single multicast.
* RCP update: The built border router image will contain an updatable RCP image and can automatically update the RCP on version mismatch or RCP failure.

In the future releases, the following features will be added:

* Web GUI
* Co-exsistence with BLE
* NAT64
