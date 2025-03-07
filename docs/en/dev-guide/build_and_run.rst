******************
2.1. Build and Run
******************

This document contains instructions on building the images for ESP Thread Border Router and CLI device and forming a Thread network with the devices.

2.1.1. Set up the Repositories
------------------------------

Clone the `esp-idf <https://github.com/espressif/esp-idf>`_ and the `esp-thread-br <https://github.com/espressif/esp-thread-br>`_ repository.

Refer to the specific tags for each stable release at `esp-thread-br releases <https://github.com/espressif/esp-thread-br/releases>`_.

Follow the steps below to obtain the latest esp-thread-br master branch and the recommended ESP-IDF version `v5.2.4 <https://github.com/espressif/esp-idf/tree/v5.2.4>`_ release:

.. code-block:: bash

   git clone --recursive https://github.com/espressif/esp-idf.git

.. code-block:: bash

   cd esp-idf

.. code-block:: bash

   git checkout v5.2.4

.. code-block:: bash

   git submodule update --init --depth 1

.. code-block:: bash

   ./install.sh

.. code-block:: bash

   . ./export.sh

.. code-block:: bash

   cd ..

.. code-block:: bash

   git clone --recursive https://github.com/espressif/esp-thread-br.git


If you are new to ESP-IDF, please follow the `ESP-IDF getting started guide <https://idf.espressif.com/>`_ to set up the IDF development environment and get familiar with the IDF development tools.

2.1.2. Build the RCP Image
--------------------------

Build the ``esp-idf/examples/openthread/ot_rcp`` example. The firmware doesn't need to be explicitly flashed to a device. It will be included in the Border Router firmware and flashed to the ESP32-H2 chip upon first boot.

.. code-block:: bash

   cd $IDF_PATH/examples/openthread/ot_rcp

Select the ESP32-H2 as the RCP.

.. code-block:: bash

   idf.py set-target esp32h2

.. code-block:: bash

   idf.py build

.. note::

   The default communication interface on the ESP Thread Border Router board uses UART0 with `baud rate = 460800`. If you are setting up a project using standalone modules, please update the UART configurations (`radio_uart_config`) in `esp_ot_config.h <https://github.com/espressif/esp-idf/blob/master/examples/openthread/ot_rcp/main/esp_ot_config.h>`_ before building the project.

2.1.3. Configure ESP Thread Border Router
-----------------------------------------

Go to the ``basic_thread_border_router`` example folder.

.. code-block:: bash

   cd esp-thread-br/examples/basic_thread_border_router

The default configuration works as is on ESP Thread Border Router board, the default SoC target is ESP32-S3.

To run the example on other SoCs, please configure the SoC target using command:

.. code-block:: bash

   idf.py set-target <chip_name>

For any other customized settings, you can configure the project in menuconfig.

.. code-block:: bash

   idf.py menuconfig

.. note::

   `LWIP_IPV6_NUM_ADDRESSES` configuration is fixed in the border router library, it was changed from 8 to 12 since IDF v5.3.1 release. Please update this configuration based on the following table:

      +----------------------------------------+-------------------------+
      |    IDF Versions                        | LWIP_IPV6_NUM_ADDRESSES |
      +----------------------------------------+-------------------------+
      | v5.1.4 and earlier                     |            8            |
      +----------------------------------------+-------------------------+
      | v5.2.2 and earlier                     |            8            |
      +----------------------------------------+-------------------------+
      | v5.3.0                                 |            8            |
      +----------------------------------------+-------------------------+
      | v5.1.5, v5.2.3, v5.3.1, v5.4 and later |            12           |
      +----------------------------------------+-------------------------+

2.1.3.1. Wi-Fi based Thread Border Router
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, it is configured as Wi-Fi based Thread Border Router.

The auto start mode is disabled by default, if you want the device connects to the configured Wi-Fi and form Thread network automatically, and then act as the border router, you need to enable the menuconfig ``ESP Thread Border Router Example -> Enable the automatic start mode in Thread Border``.

When automatic start mode is enabled, the Thread dataset, Wi-Fi SSID and password must be set in menuconfig. The corresponding options are ``Component config -> OpenThread -> Thread Operational Dataset``, ``Example Connection Configuration -> WiFi SSID`` and ``Example Connection Configuration -> WiFi Password``.

Note that in this mode, the device will first attempt to use the Wi-Fi SSID and password stored in NVS. If no Wi-Fi information is stored, it will then use the `EXAMPLE_WIFI_SSID` and `EXAMPLE_WIFI_PASSWORD` from menuconfig.

.. note::

   The following configuration options are all optional, jump to `2.1.4. Build and Run the Thread Border Router`_ if you don't need any customized settings.

2.1.3.2. Ethernet based Thread Border Router
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The border router can also be configured to connect to an Ethernet network. In this case, the daughter board ``ESP Thread Border Router/Zigbee Gateway Sub-Ethernet`` is required to extend the Ethernet interface.

The following options need to be set:

- Enable ``EXAMPLE_CONNECT_ETHERNET``
- Disable ``EXAMPLE_CONNECT_WIFI``

The configurations of ``EXAMPLE_CONNECT_ETHERNET`` as following:

    +---------------+----------------+---------------+
    |   Parameter   |     Value      |     Note      |
    +---------------+----------------+---------------+
    |      Type     |  W5500 Module  |   Mandatory   |
    +---------------+----------------+---------------+
    |   Stack Size  |      2048      |   Customized  |
    +---------------+----------------+---------------+
    |    SPI Host   |      SPI2      |   Mandatory   |
    +---------------+----------------+---------------+
    |    SPI SCLK   |     GPIO21     |   Mandatory   |
    +---------------+----------------+---------------+
    |    SPI MOSI   |     GPIO45     |   Mandatory   |
    +---------------+----------------+---------------+
    |    SPI MISO   |     GPIO38     |   Mandatory   |
    +---------------+----------------+---------------+
    |    SPI  CS    |     GPIO41     |   Mandatory   |
    +---------------+----------------+---------------+
    | SPI Interrupt |     GPIO39     |   Mandatory   |
    +---------------+----------------+---------------+
    |    SPI SPEED  |     36 MHz     |  Customized   |
    +---------------+----------------+---------------+
    |    PHY Reset  |     GPIO40     |   Mandatory   |
    +---------------+----------------+---------------+
    |  PHY Address  |        1       |   Mandatory   |
    +---------------+----------------+---------------+

The configuration result would look like this.

.. code-block:: bash

   Espressif IoT Development Framework Configuration
   [ ] connect using WiFi interface
   [*] connect using Ethernet interface
   (2048)  emac_rx task stack size
         Ethernet Type (W5500 Module)  --->
   (2)     SPI Host Number
   (21)    SPI SCLK GPIO number
   (45)    SPI MOSI GPIO number
   (38)    SPI MISO GPIO number
   (41)    SPI CS GPIO number
   (36)    SPI clock speed (MHz)
   (39)    Interrupt GPIO number
   (40)    PHY Reset GPIO number
   (1)     PHY Address
   [*] Obtain IPv6 address
        Preferred IPv6 Type (Local Link Address)  --->

2.1.3.3. Thread Network Parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Thread network parameters could be pre-configured with ``OPENTHREAD_NETWORK_xx`` options.

2.1.3.4. Communication Interface
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The default communication interface between host SoC and RCP is UART.

In order to use the SPI interface instead, the ``OPENTHREAD_RCP_SPI`` and ``OPENTHREAD_RADIO_SPINEL_SPI`` options should be enabled in ``ot_rcp`` and ``basic_thread_border_router`` example configurations, respectively. And set corresponding GPIO numbers in `esp_ot_config.h`.

2.1.3.5. RF External Coexistence
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The SDK incorporates an external coexistence feature that improves transmission performance when there is RF signal interference between Wi-Fi (ESP32-S3) and 802.15.4 (ESP32-H2).

Please refer to `external_coexistence_design_en.pdf <https://www.espressif.com.cn/sites/default/files/documentation/external_coexistence_design_en.pdf>`_ for the external coexistence design. In addition to the 3-wire mode (use request signal, grant signal and priority signal), a 4th wire tx signal is used to indicate whether the Wi-Fi SoC is under transmission state or not, it helps to enable the scenario that 802.15.4 could transmit when Wi-Fi is receiving.

.. note::

   The external coexistence feature only helps when Wi-Fi and 802.15.4 operate on close channel frequency, in which case the interference is significant. Otherwise, the feature is unnecessary.

To enable the external coexistence feature, check the ``EXTERNAL_COEX_ENABLE`` option in both ``basic_thread_border_router`` and ``ot_rcp`` examples.

The default pin configurations have been set for ESP Thread Border Router Board. The users can change the configurations through menuconfig ``ESP Thread Border Router Example → External coexist wire type and pin config`` if needed.

2.1.4. Build and Run the Thread Border Router
---------------------------------------------

Build and Flash the example to the host SoC.

.. code-block:: bash

   idf.py -p ${PORT_TO_BR} flash monitor

The following result will be shown in your terminal:

Wi-Fi Border Router:

.. code-block::

   I (555) cpu_start: Starting scheduler on PRO CPU.
   I (0) cpu_start: Starting scheduler on APP CPU.
   I (719) example_connect: Start example_connect.
   I (739) wifi:wifi firmware version: 4d93d42
   I (899) wifi:mode : sta (84:f7:03:c0:d1:e8)
   I (899) wifi:enable tsf
   I (899) example_connect: Connecting to xxxx...
   I (899) example_connect: Waiting for IP(s)
   I (5719) example_connect: Got IPv6 event: Interface "example_netif_sta" address: fe80:0000:0000:0000:86f7:03ff:fec0:d1e8, type: ESP_IP6_ADDR_IS_LINK_LOCAL
   I (5719) esp_netif_handlers: example_netif_sta ip: 192.168.1.102, mask: 255.255.255.0, gw: 192.168.1.1
   I (5729) example_connect: Got IPv4 event: Interface "example_netif_sta" address: 192.168.1.102
   I (5739) example_common: Connected to example_netif_sta
   I (5749) example_common: - IPv4 address: 192.168.1.102,
   I (5749) example_common: - IPv6 address: fe80:0000:0000:0000:86f7:03ff:fec0:d1e8, type: ESP_IP6_)
   I(5779) OPENTHREAD:[I] Platform------: RCP reset: RESET_POWER_ON
   I(5809) OPENTHREAD:[N] Platform------: RCP API Version: 6
   I (5919) esp_ot_br: RCP Version in storage: openthread-esp32/8282dca796-e64ba13fa; esp32h2;  2022-10-10 06:01:35 UTC
   I (5919) esp_ot_br: Running RCP Version: openthread-esp32/8282dca796-e64ba13fa; esp32h2;  2022-10-10 06:01:35 UTC
   I (5929) OPENTHREAD: OpenThread attached to netif
   I(5939) OPENTHREAD:[I] SrpServer-----: Selected port 53535
   I(5949) OPENTHREAD:[I] NetDataPublshr: Publishing DNS/SRP service unicast (ml-eid, port:53535)


Ethernet Border Router:

.. code-block::

   I (793) cpu_start: Starting scheduler on PRO CPU.
   I (793) cpu_start: Starting scheduler on APP CPU.
   I (904) system_api: Base MAC address is not set
   I (904) system_api: read default base MAC address from EFUSE
   I (924) esp_eth.netif.netif_glue: 70:b8:f6:12:c5:5b
   I (924) esp_eth.netif.netif_glue: ethernet attached to netif
   I (2524) ethernet_connect: Waiting for IP(s).
   I (2524) ethernet_connect: Ethernet Link Up
   I (3884) ethernet_connect: Got IPv6 event: Interface "example_netif_eth" address: fe80:0000:0000:0000:72b8:f6ff:fe12:c55b, type: ESP_IP6_ADDR_IS_LINK_LOCAL
   I (3884) esp_netif_handlers: example_netif_eth ip: 192.168.8.148, mask: 255.255.255.0, gw: 192.168.8.1
   I (3894) ethernet_connect: Got IPv4 event: Interface "example_netif_eth" address: 192.168.8.148
   I (3904) example_common: Connected to example_netif_eth
   I (3904) example_common: - IPv4 address: 192.168.8.148,
   I (3914) example_common: - IPv6 address: fe80:0000:0000:0000:72b8:f6fI(3944) OPENTHREAD:[I] Platform------: RCP reset: RESET_POWER_ON
   I(3974) OPENTHREAD:[N] Platform------: RCP API Version: 6
   I(4144) OPENTHREAD:[I] Settings------: Read NetworkInfo {rloc:0x4400, extaddr:129f848762f1c578, role:leader, mode:0x0f, version:4, keyseq:0x0, ...
   I(4154) OPENTHREAD:[I] Settings------: ... pid:0x18954426, mlecntr:0x7da7, maccntr:0x7d1c, mliid:2874d9fa90dc8093}
   I (4194) OPENTHREAD: OpenThread attached to netif

2.1.4.1. Connect the Wi-Fi and Form the Thread Network
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
If enable ``OPENTHREAD_BR_AUTO_START`` option, just skip this step.

If disable ``OPENTHREAD_BR_AUTO_START`` option, you need to setup the network manually. The following CLI commands can be used to connect Wi-Fi and form a Thread network:

.. code-block::

   wifi connect -s <ssid> -p <psk>

.. code-block::

   dataset init new

.. code-block::

   dataset commit active

.. code-block::

   ifconfig up

.. code-block::

   thread start


The BR device will connect to the Wi-Fi and then form a Thread network.

.. code-block::

   > wifi connect -s mywifi -p espressif
     ssid: mywifi
     psk: espressif
     I (5241) pp: pp rom version: e7ae62f
     I (5241) net80211: net80211 rom version: e7ae62f
     I (5251) wifi:wifi driver task: 3fcbe1a0, prio:23, stack:6144, core=0
     I (5251) wifi:wifi firmware version: 0016c4d
     I (5251) wifi:wifi certification version: v7.0
     I (5251) wifi:config NVS flash: enabled
     I (5251) wifi:config nano formating: enabled
     I (5251) wifi:Init data frame dynamic rx buffer num: 32
     I (5251) wifi:Init static rx mgmt buffer num: 5
     I (5251) wifi:Init management short buffer num: 32
     I (5251) wifi:Init dynamic tx buffer num: 32
     I (5251) wifi:Init static tx FG buffer num: 2
     I (5251) wifi:Init static rx buffer size: 1600
     I (5251) wifi:Init static rx buffer num: 10
     I (5251) wifi:Init dynamic rx buffer num: 32
     I (5251) wifi_init: rx ba win: 6
     I (5251) wifi_init: tcpip mbox: 32
     I (5251) wifi_init: udp mbox: 6
     I (5251) wifi_init: tcp mbox: 6
     I (5251) wifi_init: tcp tx win: 5760
     I (5251) wifi_init: tcp rx win: 5760
     I (5251) wifi_init: tcp mss: 1440
     I (5251) wifi_init: WiFi IRAM OP enabled
     I (5251) wifi_init: WiFi RX IRAM OP enabled
     I (5261) wifi:Set ps type: 0, coexist: 0
     I (5261) phy_init: phy_version 640,cd64a1a,Jan 24 2024,17:28:12
     I (5351) wifi:mode : null
     I (5351) wifi:mode : sta (48:27:e2:14:4d:3c)
     I (5351) wifi:enable tsf
     I (6571) wifi:new:<11,2>, old:<1,1>, ap:<255,255>, sta:<11,2>, prof:1
     I (7051) wifi:state: init -> auth (b0)
     I (7051) wifi:state: auth -> assoc (0)
     I (7071) wifi:state: assoc -> run (10)
     I (7351) wifi:connected with mywifi, aid = 2, channel 11, 40D, bssid = 94:d9:b3:1d:d4:37
     I (7351) wifi:security: WPA2-PSK, phy: bgn, rssi: -26
     I (7351) wifi:pm start, type: 0
     I (7361) wifi:dp: 1, bi: 102400, li: 3, scale listen interval from 307200 us to 307200 us
     I (7361) wifi:set rx beacon pti, rx_bcn_pti: 0, bcn_timeout: 25000, mt_pti: 0, mt_time: 10000
     I (7411) wifi:<ba-add>idx:0 (ifx:0, 94:d9:b3:1d:d4:37), tid:0, ssn:3, winSize:64
     I (7441) wifi:AP's beacon interval = 102400 us, DTIM period = 1
     I (8361) esp_netif_handlers: sta ip: 192.168.1.100, mask: 255.255.255.0, gw: 192.168.1.1
     I (8501) ot_ext_cli: Got IPv6 event: Interface "sta" address: fe80:0000:0000:0000:4a27:e2ff:fe14:4d3c
     I(8501) OPENTHREAD:[N] RoutingManager: No valid /48 BR ULA prefix found in settings, generating new one
     I(8511) OPENTHREAD:[N] RoutingManager: BR ULA prefix: fd8f:e9a2:bfcc::/48 (generated)
     I(8511) OPENTHREAD:[N] RoutingManager: Local on-link prefix: fdde:ad00:beef:cafe::/64
     wifi sta is connected successfully
     Done
     > dataset init new
     Done
     > dataset commit active
     Done                                                                                                                                                                  I (12401) OPENTHREAD: NAT64 ready
     > ifconfig up
     I (15451) OPENTHREAD: Platform UDP bound to port 49153
     Done
     I (15451) OT_STATE: netif up
     > thread start
     I(18201) OPENTHREAD:[N] Mle-----------: Role disabled -> detached
     Done
     > I(18521) OPENTHREAD:[N] Mle-----------: Attach attempt 1, AnyPartition reattaching with Active Dataset
     I(25141) OPENTHREAD:[N] RouterTable---: Allocate router id 11
     I(25141) OPENTHREAD:[N] Mle-----------: RLOC16 fffe -> 6c00
     I(25151) OPENTHREAD:[N] Mle-----------: Role detached -> leader
     I(25151) OPENTHREAD:[N] Mle-----------: Partition ID 0x82de096
     I (25161) OPENTHREAD: Platform UDP bound to port 49154


2.1.5. Build and Run the Thread CLI Device
------------------------------------------

Build the ``esp-idf/examples/openthread/ot_cli`` example and flash the firmware to another ESP32-H2 devkit.


.. code-block:: bash

   cd $IDF_PATH/examples/openthread/ot_cli


.. code-block:: bash

   idf.py -p ${PORT_TO_ESP32_H2} flash monitor


2.1.6. Attach the CLI Device to the Thread Network
--------------------------------------------------

First acquire the Thread network dataset on the Border Router:

.. code-block::

   dataset active -x


The network data will be printed on the Border Router:

.. code-block::

   > dataset active -x
   0e080000000000010000000300001335060004001fffe00208dead00beef00cafe0708fdfaeb6813db063b0510112233445566778899aabbccddeeff00030f4f70656e5468726561642d34396436010212340410104810e2315100afd6bc9215a6bfac530c0402a0f7f8
   Done


Commit the dataset on the CLI device with the acquired dataset:

.. code-block::

   dataset set active 0e080000000000010000000300001335060004001fffe00208dead00beef00cafe0708fdfaeb6813db063b0510112233445566778899aabbccddeeff00030f4f70656e5468726561642d34396436010212340410104810e2315100afd6bc9215a6bfac530c0402a0f7f8


Set the network data active on the CLI device:

.. code-block::

   dataset commit active


Set up the network interface on the CLI device:

.. code-block::

   ifconfig up


Start the thread network on the CLI device:

.. code-block::

   thread start


The CLI device will become a child or a router in the Thread network:

.. code-block::

   > dataset set active 0e080000000000010000000300001335060004001fffe00208dead00beef00cafe0708fdfaeb6813db063b0510112233445566778899aabbccddeeff00030f4f70656e5468726561642d34396436010212340410104810e2315100afd6bc9215a6bfac530c0402a0f7f8
   Done
   > dataset commit active
   Done
   > ifconfig up
   Done
   I (1665530) OPENTHREAD: netif up
   > thread start
   I(1667730) OPENTHREAD:[N] Mle-----------: Role disabled -> detached
   Done
   > I(1669240) OPENTHREAD:[N] Mle-----------: RLOC16 5800 -> fffe
   I(1669590) OPENTHREAD:[N] Mle-----------: Attempt to attach - attempt 1, AnyPartition
   I(1670590) OPENTHREAD:[N] Mle-----------: RLOC16 fffe -> 6c01
   I(1670590) OPENTHREAD:[N] Mle-----------: Role detached -> child
