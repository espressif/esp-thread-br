# OpenThread Border Router Example

## Overview

This example demonstrates an [OpenThread border router](https://openthread.io/guides/border-router).

## How to use example

### Hardware Required

Please refer to [ESP Thread Border Router Hardware](../../README.md##Hardware-Platforms), the ESP Thread Border Router Board is recommended for this example.

### Set up ESP IDF

Refer to [ESP-IDF Get Started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html).

Currently, it is recommended to use ESP-IDF [v5.2.4](https://github.com/espressif/esp-idf/tree/v5.2.4) release.

### Configure the project

ESP32-S3 is the host SoC on ESP Thread Border Router Board, esp32s3 is selected by default in the example. Set the other target with this command:

```
idf.py set-target <target>
```

`LWIP_IPV6_NUM_ADDRESSES` configuration is fixed in the border router library, it was changed from 8 to 12 since IDF v5.3.1 release. Please update this configuration based on the following table:

|     IDF Versions                         |  LWIP_IPV6_NUM_ADDRESSES  |
|:----------------------------------------:|:-------------------------:|
|  v5.1.4 and earlier                      |            8              |
|  v5.2.2 and earlier                      |            8              |
|  v5.3.0                                  |            8              |
|  v5.1.5, v5.2.3, v5.3.1, v5.4 and later  |            12             |

The host could be pre-configured with `OPENTHREAD_RADIO_SPINEL_UART` or `OPENTHREAD_RADIO_SPINEL_SPI` to select UART or SPI to access the Radio Co-Processor.

If the `OPENTHREAD_BR_AUTO_START` option is enabled, the device will connect to the configured Wi-Fi and form Thread network automatically then act as the border router:
- The Wi-Fi network's ssid and psk needs to be pre-configured with `EXAMPLE_WIFI_SSID` and `EXAMPLE_WIFI_PASSWORD`. In this mode, the device will first attempt to use the Wi-Fi SSID and password stored in NVS. If no Wi-Fi information is stored, it will then use the pre-configured ssid and psk.
- The Thread network parameters could be pre-configured with `OPENTHREAD_NETWORK_xx` options.

If the `OPENTHREAD_BR_START_WEB` option is enabled, [ESP Thread Border Router Web Server](../../components/esp_ot_br_server/README.md) will be provided to configure and query Thread network via a Web GUI.

The `ESP_CONSOLE_USB_SERIAL_JTAG` option is enabled by default for ESP Thread Border Router Hardware. If you are using other hardware, you may enable `ESP_CONSOLE_UART_DEFAULT` if you wish to use the UART port for serial communication instead. In either case, `OPENTHREAD_CONSOLE_TYPE_USB_SERIAL_JTAG` or `OPENTHREAD_CONSOLE_TYPE_UART` will be selected accordingly. If you enable `ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG`, please manually specify which `OPENTHREAD_CONSOLE_TYPE` you would like to use.

### Create the RCP firmware image

The border router supports updating the RCP upon boot.

First build the [ot_rcp](https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_rcp) example in IDF. In the building process, the built RCP image will be automatically packed into the border router firmware.

### Build, Flash, and Run

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT build erase-flash flash monitor
```

If the `OPENTHREAD_BR_AUTO_START` option is not enabled, you need to manually configure the networks with CLI commands.

`wifi` command can be used to configure the Wi-Fi network.

```bash
> wifi
--wifi parameter---
connect
-s                   :     wifi ssid
-p                   :     wifi psk
---example---
join a wifi:
ssid: threadcertAP
psk: threadcertAP    :     wifi connect -s threadcertAP -p threadcertAP
state                :     get wifi state, disconnect or connect
---example---
get wifi state       :     wifi state
Done
```

To join a Wi-Fi network, please use the `wifi connect` command:

```bash
> wifi connect -s threadcertAP -p threadcertAP
ssid: threadcertAP
psk: threadcertAP
I (11331) wifi:wifi driver task: 3ffd06e4, prio:23, stack:6656, core=0
I (11331) system_api: Base MAC address is not set
I (11331) system_api: read default base MAC address from EFUSE
I (11341) wifi:wifi firmware version: 45c46a4
I (11341) wifi:wifi certification version: v7.0


..........

I (13741) esp_netif_handlers: sta ip: 192.168.3.10, mask: 255.255.255.0, gw: 192.168.3.1
W (13771) wifi:<ba-add>idx:0 (ifx:0, 02:0f:c1:32:3b:2b), tid:0, ssn:2, winSize:64
wifi sta is connected successfully
Done
```

To get the state of the Wi-Fi network:

```bash
> wifi state
connected
Done
```

For forming the Thread network, please refer to the [ot_cli_README](https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_cli):

```bash
> ifconfig up
Done
> thread start
Done
```


## Example Output

```bash
I (2729) esp_netif_handlers: example_connect: sta ip: 192.168.1.100, mask: 255.255.255.0, gw: 192.168.1.1
I (2729) example_connect: Got IPv4 event: Interface "example_connect: sta" address: 192.168.1.100
I (3729) example_connect: Got IPv6 event: Interface "example_connect: sta" address: fe80:0000:0000:0000:266f:28ff:fe80:2920, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (3729) example_connect: Connected to example_connect: sta
I (3739) example_connect: - IPv4 address: 192.168.1.100
I (3739) example_connect: - IPv6 address: fe80:0000:0000:0000:266f:28ff:fe80:2920, type: ESP_IP6_ADDR_IS_LINK_LOCAL

......


I(8139) OPENTHREAD:[INFO]-MLE-----: AttachState ParentReqReeds -> Idle
I(8139) OPENTHREAD:[NOTE]-MLE-----: Allocate router id 50
I(8139) OPENTHREAD:[NOTE]-MLE-----: RLOC16 fffe -> c800
I(8159) OPENTHREAD:[NOTE]-MLE-----: Role Detached -> Leader
```

## Bidirectional IPv6 connectivity

The border router will automatically publish the prefix and the route table rule to the Wi-Fi network via ICMPv6 router advertisement packages.

### Host configuration

To automatically configure your host's route table rules you need to set these sysctl options:

Please replace `wlan0` with the real name of your Wi-Fi network interface.
```
sudo sysctl -w net/ipv6/conf/wlan0/accept_ra=2
sudo sysctl -w net/ipv6/conf/wlan0/accept_ra_rt_info_max_plen=128
```

For mobile devices, the route table rules will be automatically configured after iOS 14 and Android 8.1.


### Testing IPv6 connectivity

Now in the joining device, check the IP addresses:

```
> ipaddr
fde6:75ff:def4:3bc3:9e9e:3ef:4245:28b5
fdde:ad00:beef:0:0:ff:fe00:c402
fdde:ad00:beef:0:ad4a:9a9a:3cd6:e423
fe80:0:0:0:f011:2951:569e:9c4a
```

You'll notice an IPv6 global prefix with only on address assigned under it. This is the routable address of this Thread node.
You can ping this address on your host:

``` bash
$ ping fde6:75ff:def4:3bc3:9e9e:3ef:4245:28b5
PING fde6:75ff:def4:3bc3:9e9e:3ef:4245:28b5(fde6:75ff:def4:3bc3:9e9e:3ef:4245:28b5) 56 data bytes
64 bytes from fde6:75ff:def4:3bc3:9e9e:3ef:4245:28b5: icmp_seq=1 ttl=63 time=459 ms
64 bytes from fde6:75ff:def4:3bc3:9e9e:3ef:4245:28b5: icmp_seq=2 ttl=63 time=109 ms
64 bytes from fde6:75ff:def4:3bc3:9e9e:3ef:4245:28b5: icmp_seq=3 ttl=63 time=119 ms
64 bytes from fde6:75ff:def4:3bc3:9e9e:3ef:4245:28b5: icmp_seq=4 ttl=63 time=117 ms
```

## Service discovery

The newly introduced service registration protocol([SRP](https://datatracker.ietf.org/doc/html/draft-ietf-dnssd-srp-10)) allows devices in the Thread network to register a service. The border router will forward the service to the Wi-Fi network via mDNS.

Now we'll publish the service `my-service._test._udp` with hostname `test0` and port 12345

```
> srp client host name test0
Done
> srp client host address fde6:75ff:def4:3bc3:9e9e:3ef:4245:28b5
Done
> srp client service add my-service _test._udp 12345
Done
> srp client autostart enable
Done
```

This service will also become visible on the Wi-Fi network:

```bash
$ avahi-browse -r _test._udp -t

+ enp1s0 IPv6 my-service                                    _test._udp           local
= enp1s0 IPv6 my-service                                    _test._udp           local
   hostname = [test0.local]
   address = [fde6:75ff:def4:3bc3:9e9e:3ef:4245:28b5]
   port = [12345]
   txt = []
+ enp1s0 IPv4 my-service                                    _test._udp           local
= enp1s0 IPv4 my-service                                    _test._udp           local
   hostname = [test0.local]
   address = [fde6:75ff:def4:3bc3:9e9e:3ef:4245:28b5]
   port = [12345]
   txt = []
```

## Updating the border router from HTTPS server

With configuration `CREATE_OTA_IMAGE_WITH_RCP_FW` on, the border router OTA image will be automatically created at `build/ota_with_rcp_image`. The file is a bundle of the border router firmware and the RCP firmware.
To test with a local https server, you need to first create a folder with the image.

Enter this folder and create a new self-signed certificate and key with the following command:

```bash
$ openssl req -x509 -newkey rsa:2048 -keyout ca_key.pem -out ca_cert.pem -days 365 -nodes
```

Note that when prompted for the `Common Name(CN)`, the name entered shall match the hostname running the server.

Now launch the HTTPS server
```bash
openssl s_server -WWW -key ca_key.pem -cert ca_cert.pem -port 8070
```

Replace `server_certs/ca_cert.pem` with the one generated by the command. Then build and flash the device.

To download the image from the server, run the following command on the border router:

```
> ota download https://${HOST_URL}:8070/ota_with_rcp_image
```

After downloading the device will restart and update itself with the new firmware. The RCP will also be updated if the firmware version changes.

## Updating the rcp without restarting the Host

The border router provides a mechanism to manually update the rcp firmware without restarting the Host.

Ensure that Thread is stopped and the interface is disabled.

```bash
(Optional)
> thread stop

I(20618) OPENTHREAD:[N] Mle-----------: Role leader -> detached
I(20618) OPENTHREAD:[N] Mle-----------: Role detached -> disabled
Done

> ifconfig down

Done
I (24488) OT_STATE: netif u
```

Then update the rcp firmware with the command `otrcp update`.

```bash
> otrcp update
...
...
Done
```

After updating, the interface can be enabled and the Thread can be started.

```bash
> ifconfig up

I (576848) OPENTHREAD: Platform UDP bound to port 49155
Done
I (576848) OT_STATE: netif up
> thread start

I(579858) OPENTHREAD:[N] Mle-----------: Role disabled -> detached
Done
```