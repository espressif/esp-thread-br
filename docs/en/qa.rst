******
5. FAQ
******

5.1. Can't ping the Wi-Fi device using the OT CLI `ping` command
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Espressif Thread Border Router (BR) framework integrates the OpenThread stack and the Wi-Fi stack as network interfaces (Netif) connected to lwIP, with both OpenThread and Wi-Fi addresses managed by lwIP.

.. code-block::

   -----------                                          ------------
   |         |                                          |   lwIP   |
   |         |                                          |   -----  |
   |         |                                          |   |   |  |
   |  Wi-Fi  |                                          |-↑-↓---↑--|
   |  Device | <--- Ping Request to Wi-Fi Device ------ | |   | |  |
   |         | ----------- Ping Reply to BR ----------> |-|   | |--|<---- Ping command is
   |         |                                          | WiFi| OT |      generated at the OT layer
   |         |                                          |     |    |
   -----------                                          ------------

lwIP serves as the upper layer for both OpenThread and Wi-Fi, handling packet forwarding between network interfaces. Here is how it works in this setup:

1. The ping command is generated at the OpenThread layer, and the ping packet is sent to lwIP for forwarding.
2. lwIP recognizes that the destination address belongs to the Wi-Fi Netif and forwards the packet to the Wi-Fi interface.
3. The peer Wi-Fi device receives the ping packet and sends a reply back to the BR.
4. The BR receives the reply packet on the Wi-Fi interface and forwards it to lwIP for processing.
5. lwIP identifies that the destination address is assigned to the BR itself (since OpenThread assigns the OMR address to lwIP). Because of this, lwIP does not forward the reply packet back to the OpenThread layer.
6. Since the OpenThread layer does not receive the reply packet, it incorrectly shows the ping response as lost, even though the BR successfully receives the reply.

To see a successful ping reply from the Wi-Fi device to BR, you need to implement the ping command at the lwIP layer instead. This allows you to directly ping the Wi-Fi device from lwIP, bypassing the OpenThread stack.

5.2. Host SoC can't communicate with RCP properly
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you set up the BR using standalone modules manually, please update the GPIO pin configurations (`radio_uart_config`) in `esp_ot_config.h` for both the ot_rcp and BR examples to accurately reflect the GPIO connections between the Wi-Fi and 802.15.4 SoCs.

The Host and RCP will be unable to communicate if the communication interface is not configured correctly, resulting in warning logs similar to the following:

.. code-block::

   W(2499) OPENTHREAD:[W] P-SpinelDrive-: Wait for response timeout

.. note::

   GPIO17 and GPIO18 on the ESP32-S3 have a different driver current (refer to `ESP32-S3 TRM, Chapter 6.12 <https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf>`_). If an ESP32-S3 is used as the host, GPIO4 and GPIO5 are recommended for use as the UART RX/TX GPIOs.

5.3. Using ot_rcp with OTBR (ot-br-posix)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The **OpenThread Border Router (OTBR)** is a Linux-based Thread BR solution. Setup instructions can be found here: `OpenThread Border Router Build and Configuration <https://openthread.io/guides/border-router/build>`_.

The **ESP Thread RCP** (`ot_rcp <https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_rcp>`_) can also function as the RCP in an OTBR setup. However, the `otbr-agent` configuration needs to be adjusted accordingly:

- The default `otbr-agent` configuration uses `/dev/ttyACM0`. Each ESP devkit has two ports:
   - A **USB port**, recognized as `/dev/ttyACMx`
   - A **UART port**, recognized as `/dev/ttyUSBx`

- The default baudrate in `otbr-agent`` is 115200, while the default baudrate in (`ot_rcp <https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_rcp>`_) example is 460800

Please refer to `Attach and configure RCP device <https://openthread.io/guides/border-router/build#attach-and-configure-rcp-device>`_ and update the UART configuration to use the correct USB port number (i.e. replace `/dev/ttyUSB0` with `/dev/ttyUSBx`) and baud rate in the `otbr-agent` configuration file `/etc/default/otbr-agent`:

.. code-block::

   OTBR_AGENT_OPTS="-I wpan0 -B wlan0 spinel+hdlc+uart:///dev/ttyUSB0?uart-baudrate=460800"

Restart the `otbr-agent` service, and confirm it works with the RCP now:

.. code-block::

   ➜ sudo service otbr-agent status
   ● otbr-agent.service - OpenThread Border Router Agent
     Loaded: loaded (/lib/systemd/system/otbr-agent.service; enabled; vendor preset: enabled)
     Active: active (running) since Fri 2025-02-28 17:45:20 CST; 29min ago
     Process: 1160169 ExecStartPre=/usr/sbin/service mdns start (code=exited, status=0/SUCCESS)
     Main PID: 1160178 (otbr-agent)
      Tasks: 1 (limit: 18871)
     Memory: 920.0K
        CPU: 395ms
     CGroup: /system.slice/otbr-agent.service
             └─1160178 /usr/sbin/otbr-agent -I wpan0 -B wlx0013eff10f89 "spinel+hdlc+uart:///dev/ttyUSB0?uart-baudrate=460800" trel://wlx0013eff10f89

   Feb 28 18:13:05 madcow-OptiPlex-3070 otbr-agent[1160178]: 00:27:44.764 [I] MeshForwarder-:     dst:[ff02:0:0:0:0:0:0:1]:19788
   Feb 28 18:13:10 madcow-OptiPlex-3070 otbr-agent[1160178]: 00:27:49.324 [I] MeshForwarder-: Received IPv6 UDP msg, len:83, chksum:8c92, ecn:no, from:3618183b10a40a22, sec:no, prio:net, rss:-70.0
