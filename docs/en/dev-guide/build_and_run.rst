*******************
2.1. Build and Run
*******************

This document contains instructions on building the images for ESP Thread Border Router and CLI device and forming a Thread network with the devices.

Set up the Repositories
------------------------

Clone the `esp-idf <https://github.com/espressif/esp-idf>`_ and the `esp-thread-br <https://github.com/espressif/esp-thread-br>`_ repository.

.. code-block:: bash

   git clone https://github.com/espressif/esp-idf.git


.. code-block:: bash

   git clone --recursive https://github.com/espressif/esp-thread-br.git


Follow the `ESP-IDF getting started guide <https://idf.espressif.com/>`_ to set up the IDF development environment.

Build the RCP Image
---------------------

Build the ``esp-idf/examples/openthread/ot-rcp`` example. The firmware doesn't need to be explicitly flashed to a device. It will be included in the Border Router firmware and flashed to the ESP32-H2 chip upon first boot.

.. code-block:: bash

   cd esp-idf/examples/openthread/ot-rcp


.. code-block:: bash

   idf.py build


Build and Run the ESP Thread Border Router
-------------------------------------------

The ESP Thread Border Router is configured to connect to the Wi-Fi network and form a Thread network upon boot by default. The Wi-Fi SSID and password must be set using ``idf.py menuconfig`` before building. The corresponding options are ``Example Connection Configuration -> WiFi SSID`` and ``Example Connection Configuration -> WiFi Password``.

Build the ``esp-thread-br/examples/basic_thread_border_router`` example. Flash the example to the ``ESP-Thread-Border-Router`` devkit.

.. code-block:: bash

   cd esp-thread-br/examples/basic_thread_border_router


.. code-block:: bash

   idf.py -p ${PORT_TO_BR} flash monitor


Build and Run the Thread CLI Device
------------------------------------

Build the ``esp-idf/examples/openthread/ot-cli`` example and flash the firmware to a ESP32-H2 devkit.


.. code-block:: bash

   cd esp-idf/examples/openthread/ot-cli


.. code-block:: bash

   idf.py -p ${PORT_TO_ESP32_H2} flash monitor


Attach the CLI Device to the Thread Network
---------------------------------------------

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

