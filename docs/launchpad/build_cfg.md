# ESP Thread Border Router

Connect the host ESP32-S3 USB port and select firmware matching your board's flash size.

| Firmware | Hardware | Features |
| --- | --- | --- |
| `basic_thread_border_router_8mb` (default) | ESP Thread Border Router v1.2, 8MB | Web GUI, OTA |
| `basic_thread_border_router_4mb` | ESP Thread Border Router v1.2, 4MB | Web GUI, no OTA |
| `m5stack_thread_border_router` | M5Stack Thread Border Router (M5Stack CoreS3 + ESP32-H2)  | Touchscreen and Wi-Fi setup |

For basic Thread BR, configure Wi-Fi through the [CLI](https://github.com/espressif/esp-thread-br/tree/main/examples/basic_thread_border_router), then access the Web GUI using the host name ([esp-ot-br.local](http://esp-ot-br.local)) or Wi-Fi IP address.

For M5Stack Thread BR, follow the touchscreen instructions to configure Wi-Fi and manage the Thread network.
