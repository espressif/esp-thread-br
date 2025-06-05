# OpenThread Border Router M5Stack Example

## Overview

This example is based on the M5Stack CoreS3 and leverages its touchscreen to demonstrate the Thread Credential Sharing feature, in addition to basic Thread BR functionality.

It connects to a pre-configured Wi-Fi network and automatically forms a Thread network. The ePSKc can be generated via the touchscreen and is used to establish a DTLS session with a Thread Commissioner. Thread credentials can then be retrieved or configured through the DTLS session securely.

## How to Use the Example

### Hardware Required

This example requires the M5Stack CoreS3 along with the ESP32-H2 Thread/Zigbee Gateway Module. Please refer to:

- [M5Stack CoreS3](https://shop.m5stack.com/products/m5stack-cores3-esp32s3-lotdevelopment-kit)
- [ESP32-H2 Thread/Zigbee Gateway Module](https://shop.m5stack.com/products/esp32-h2-thread-zigbee-gateway-module)

### IDF Version Required

This example requires [IDF v5.4.1](https://github.com/espressif/esp-idf/tree/v5.4.1). It may not work with later IDF versions.

### Configure the Project

ESP32-S3 is the SoC on M5Stack CoreS3, so set target to esp32s3:

```sh
idf.py set-target esp32s3
```
The example is pre-configured via `sdkconfig.defaults`, and most configurations do not need to be modified using `idf.py menuconfig`. You can modify the following parameters as needed:

1. Wi-Fi SSID and PSK.
2. Initial OpenThread dataset, such as the `networkkey` and `channel`.
3. Lifetime and port of the Ephemeral Key, which can be configured via `menuconfig -> ESP Thread Border Router M5Stack Example`.

### Create the RCP Firmware Image

First build the [ot_rcp](https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_rcp) example in IDF. In the building process, the built RCP image will be automatically packed into the Border Router firmware.

### Build, Flash, and Run

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py build

idf.py -p PORT erase-flash flash monitor
```

Upon startup, the device will connect to the pre-configured Wi-Fi network and join the Thread network based on the current OpenThread NVS data. It will then publish the MeshCoP service on the Wi-Fi network. Once completed, the device will display its web server URL on the `Wi-Fi` page (see [ESP Thread Border Router WEB GUI](https://docs.espressif.com/projects/esp-thread-br/en/latest/codelab/web-gui.html)) and provide the following four clickable buttons:

1. `factoryreset`: This button is used to clear the current OpenThread NVS data and restart the device. After rebooting, the device will join the OpenThread network configured by the user through menuconfig.
2. `Ephemeral Key`: This button is used to generate an OpenThread ephemeral key and publish the `meshcop-e` service in the Wi-Fi network. The `meshcop-e` service will include the device's IP address and port number. Two modes are supported for generating the ephemeral key:
   1. `Random Key`: This mode randomly generates a 9-digit key as the OpenThread ephemeral key.
   2. `Custom Key`: This mode allows the user to input an 8-digit number and calculates a checksum digit. The resulting 9-digit number is used as the OpenThread ephemeral key.
3. `Wi-Fi`: This button is used to view and configure basic Wi-Fi parameters.
4. `Thread`: This button is used to view and configure basic Thread parameters.

This example can be used in conjunction with an external Commissioner such as `ot-commissioner`. The ephemeral key allows the Commissioner to connect to the Thread Border Router over Wi-Fi. Once connected, the Commissioner can retrieve or configure parameters for the Border Router and its child nodes via the same network.
