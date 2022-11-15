******************
2.1. Build and Run
******************

This document contains instructions on building the images for ESP Thread Border Router and CLI device and forming a Thread network with the devices.

2.1.1. Set up the Repositories
------------------------------

Clone the `esp-idf <https://github.com/espressif/esp-idf>`_ and the `esp-thread-br <https://github.com/espressif/esp-thread-br>`_ repository.

.. code-block:: bash

   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   git checkout 14ef8d56cab200d61afdc25c50ede88e0fc61c20
   git submodule update --init --recursive
   ./install.sh
   cd ..

.. code-block:: bash

   git clone --recursive https://github.com/espressif/esp-thread-br.git


Follow the `ESP-IDF getting started guide <https://idf.espressif.com/>`_ to set up the IDF development environment.

2.1.2. Build the RCP Image
--------------------------

The default communication interface is port 0 of ESP32-H2 UART running at 115200 baud, change the configuration in `esp_ot_config.h <https://github.com/espressif/esp-idf/blob/master/examples/openthread/ot_rcp/main/esp_ot_config.h>`_ if you want to use another interface or need different communication parameters.

Build the ``esp-idf/examples/openthread/ot-rcp`` example. The firmware doesn't need to be explicitly flashed to a device. It will be included in the Border Router firmware and flashed to the ESP32-H2 chip upon first boot.

.. code-block:: bash

   cd $IDF_PATH/examples/openthread/ot-rcp


.. code-block:: bash

   idf.py build


2.1.3. Configure ESP Thread Border Router
-----------------------------------------

Go to the ``basic_thread_border_router`` example folder.

.. code-block:: bash

   cd esp-thread-br/examples/basic_thread_border_router

The default configuration works as is on ESP Thread Border Router board.

For any customized settings, you can configure the project in menuconfig.

.. code-block:: bash

   idf.py menuconfig

Set ``PIN_TO_RESET``, ``PIN_TO_BOOT``, ``PIN_TO_RX``, ``PIN_TO_TX`` to corresponding GPIO numbers that connect to RCP.

The Thread network parameters could be pre-configured with ``OPENTHREAD_NETWORK_xx`` options.

2.1.3.1. Wi-Fi based Thread Border Router
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, it is configured as Wi-Fi based Thread Border Router.

If the ``OPENTHREAD_BR_AUTO_START`` option is enabled, the device will connect to the configured Wi-Fi and form Thread network automatically then act as the border router. The Wi-Fi SSID and password must be set in menuconfig. The corresponding options are ``Example Connection Configuration -> WiFi SSID`` and ``Example Connection Configuration -> WiFi Password``.

2.1.3.2. Ethernet based Thread Border Router
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The border router can also be configured to connect to Ethernet network. In this case, a host device with Ethernet interface is required, such as `ESP32-Ethernet-Kit <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-ethernet-kit.html>`_.

The following options need to be set:

- Enable ``EXAMPLE_CONNECT_ETHERNET``
- Disable ``EXAMPLE_CONNECT_WIFI``


2.1.4. Build and Run the ESP Thread Border Router
-------------------------------------------------

Build and Flash the example to the host SoC, either the ESP32-S3 on the ESP-Threa-Border-Router board, or the host SoC in your manual setup.

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


2.1.5. Build and Run the Thread CLI Device
------------------------------------------

Build the ``esp-idf/examples/openthread/ot-cli`` example and flash the firmware to another ESP32-H2 devkit.


.. code-block:: bash

   cd $IDF_PATH/examples/openthread/ot-cli


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

