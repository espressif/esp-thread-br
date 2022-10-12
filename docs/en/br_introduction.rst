*******************************************
1. Introduction to ESP Thread Border Router
*******************************************

1.1. Software Components
------------------------

.. figure:: ../images/esp-thread-border-router-solution.png
   :align: center
   :alt: ESP-Thread-Border-Router Software Components
   :figclass: align-center

   ESP-Thread-Border-Router Software Components

The ESP Thread Border Router SDK is built on top of `ESP-IDF <https://github.com/espressif/esp-idf>`_ and `OpenThread <https://github.com/openthread/openthread>`_. The OpenThread port and ESP Border Router implementation is provided as pre-built library in ESP-IDF.

1.2. Hardware Platforms
-----------------------

The ESP Thread Border Router supports both Wi-Fi and Ethernet interfaces.

1.2.1. Wi-Fi based Thread Border Router
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Wi-Fi based ESP Thread Border Router consists of two SoCs:

   - The host Wi-Fi SoC, which can be ESP32, ESP32-S and ESP32-C series SoC.
   - The radio co-processor (RCP), which is an ESP32-H series SoC. The RCP enables the Border Router to access the 802.15.4 physical and MAC layer.

Espressif provides a Border Router board which integrates the host SoC and the RCP into one module.

.. figure:: ../images/esp-thread-border-router-board.png
   :align: center
   :alt: ESP-Thread-Border-Router Board
   :figclass: align-center

   ESP-Thread-Border-Router Board

The SDK also supports manually connecting an ESP32-H2 RCP to an ESP32 series Wi-Fi SoC.

.. figure:: ../images/thread-border-router-esp32-esp32h2.jpg
   :align: center
   :alt: ESP-Thread-Border-Router Standalone Modules
   :figclass: align-center

   ESP-Thread-Border-Router Standalone Modules

1.2.2. Ethernet based Thread Border Router
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Similar to the previous Wi-Fi based Thread Border Route setup, but a device with Ethernet interface is required, such as `ESP32-Ethernet-Kit <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-ethernet-kit.html>`_.

1.3. Key Features
-----------------

The ESP Thread Border Router solution is Thread 1.3 compliant, which supports the Matter application.

It provides the following features:

  - Bi-directional IPv6 connectivity to the Wi-Fi/Ethernet network
  - Service discovery on the Wi-Fi/Ethernet network and the Thread network
  - Multicast forwarding between the Wi-Fi/Ethernet and the Thread network
  - Access to the IPv4 Internet (NAT64)
  - Updating the Border Router and the RCP from the cloud

1.4. Web GUI
------------

.. todo::
   Add introduction to Web GUI after implementing it.
