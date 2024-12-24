*******************************************
3.6. Thread Radio Encapsulation Link (TREL)
*******************************************

TREL provides the mechanism for encapsulating IEEE 802.15.4 MAC frames
within UDP/IPv6 packets. This encapsulation allows Thread messages to
traverse IP-based networks while remaining compatible with existing Thread
devices, once the Thread messages are again de-encapsulated from the
UDP/IPv6 packet.

Prerequisites
-------------

Hardware Prerequisites
^^^^^^^^^^^^^^^^^^^^^^

To perform Thread Radio Encapsulation Link (TREL), the following devices are required:

- An ESP Thread Border Router (BR) flashed with the `Thread BR Example <https://github.com/espressif/esp-thread-br/blob/main/examples/basic_thread_border_router/README.md>`_
- A Thread CLI device (e.g. ESP32-H2) flashed with the `Thread CLI Example <https://github.com/espressif/esp-idf/blob/master/examples/openthread/ot_cli/README.md>`_
- A TREL enabled Wi-Fi device (e.g. ESP32-S3) flashed with the `TREL Example <https://github.com/espressif/esp-idf/blob/master/examples/openthread/ot_trel/README.md>`_

    - Note that this TREL Wi-Fi device does not need to have built-in 802.15.4 capabilities

The Border Router and the Wi-Fi device shall be connected to the same Wi-Fi network and 
the Thread CLI device shall join the Thread network formed by the Border Router (refer to set up below for detailed instructions):

1. Thread BR connects to Wi-Fi network (refer to configuration below), forms a Thread network, and assumes the role of LEADER
2. Thread CLI device joins Thread network formed by Thread BR
3. TREL Wi-Fi device connects to the same Wi-Fi network, and joins the Thread network created by the BR using Thread CLI

Configuration
^^^^^^^^^^^^^

The Thread BR should be configured to enable TREL feature, which can be found in:

.. code-block::

    idf.py menuconfig
        → Component config 
        → OpenThread 
        → OpenThread 
        → Thread Core Features 
        → Thread Trel Radio Link 
        → Enable Thread Radio Encapsulation Link (TREL)

Both the Thread BR and Wi-Fi device should be configured to join the same Wi-Fi network using:

.. code-block::

    idf.py menuconfig
        → Example Connection Configuration 
        → WiFi SSID
    idf.py menuconfig
        → Example Connection Configuration 
        → WiFi Password

If users do not wish to provide Wi-Fi credentials via menuconfig, the credentials may also be 
provided at runtime via:

.. code-block::

    > wifi connect -s <ssid> -p <psk>

Set up
------

ESP Thread BR device
^^^^^^^^^^^^^^^^^^^^

1. Build and flash the Thread BR Example on the ESP Thread BR device, and wait a few moments for it to connect to the Wi-Fi network automatically (if configured in ``menuconfig``). 
2. Check that the ESP Thread BR device is connected to the Wi-Fi network:

.. code-block::

    > wifi state
    connected
    Done

3. Use the ESP Thread BR device to form a Thread network: 

.. code-block::

    > ifconfig up
    Done
    > thread start
    Done

4. Check that the ESP Thread BR device has assumed the role of LEADER in the Thread network:

.. code-block::

    > state
    leader
    Done

5. Obtain the Thread network details for use later:

.. code-block::

    > dataset active -x
    0e080000000000010000000300001835060004001fffe00208fe7bb701f5f1125d0708fd75cbde7c6647bd0510b3914792d44f45b6c7d76eb9306eec94030f4f70656e5468726561642d35383332010258320410e35c581af5029b054fc904a24c2b27700c0402a0fff8

Thread CLI device
^^^^^^^^^^^^^^^^^

6. Build and flash the Thread CLI Example on the Thread CLI device. 
7. Set the Thread network details on the Thread CLI device, and wait a few moments for it to join the Thread network:

.. code-block::

    > dataset set active 0e080000000000010000000300001835060004001fffe00208fe7bb701f5f1125d0708fd75cbde7c6647bd0510b3914792d44f45b6c7d76eb9306eec94030f4f70656e5468726561642d35383332010258320410e35c581af5029b054fc904a24c2b27700c0402a0fff8
    > ifconfig up
    Done
    > thread start
    Done

8. Check that the Thread CLI device has joined the Thread network and assumed the role of ROUTER or CHILD:

.. code-block::

    > state
    router # child is also a valid state
    Done

TREL enabled Wi-Fi device
^^^^^^^^^^^^^^^^^^^^^^^^^

9. Build and flash the TREL Example on the TREL enabled Wi-Fi device, and wait a few moments for it to connect to the Wi-Fi network automatically (if configured in ``menuconfig``). 
10. Check that the TREL enabled Wi-Fi device is connected to the Wi-Fi network:

.. code-block::

    > wifi state
    connected
    Done

11. Set the Thread network details on the TREL enabled Wi-Fi device, and wait a few moments for it to join the Thread network:

.. code-block::

    > dataset set active 0e080000000000010000000300001835060004001fffe00208fe7bb701f5f1125d0708fd75cbde7c6647bd0510b3914792d44f45b6c7d76eb9306eec94030f4f70656e5468726561642d35383332010258320410e35c581af5029b054fc904a24c2b27700c0402a0fff8
    > ifconfig up
    Done
    > thread start
    Done

12. Check that the Thread CLI device has joined the Thread network and assumed the role of ROUTER or CHILD:

.. code-block::

    > state
    router # child is also a valid state
    Done

Validate the Connection between Wi-Fi device and Thread CLI device
------------------------------------------------------------------

If the Wi-Fi device has been set up correctly to join the same Thread network as the Thread BR 
and CLI device, it should be possible to ping the Thread CLI device from the Wi-Fi device (and 
vice versa). 

Obtain IPv6 address of Thread CLI device:

.. code-block::

    > ipaddr mleid

    fd14:c8eb:d14c:5fbe:b57e:1e02:a532:26d1
    Done

Obtain IPv6 address of Wi-Fi device:

.. code-block::

    > ipaddr mleid

    fd14:c8eb:d14c:5fbe:bd5e:16de:3183:694a
    Done

Ping Wi-Fi device from Thread CLI device:

.. code-block::

    > ping fd14:c8eb:d14c:5fbe:bd5e:16de:3183:694a

    16 bytes from fd14:c8eb:d14c:5fbe:bd5e:16de:3183:694a: icmp_seq=1 hlim=255 time=122ms
    1 packets transmitted, 1 packets received. Packet loss = 0.0%. Round-trip min/avg/max = 122/122.0/122 ms.
    Done

Ping Thread CLI device from Wi-Fi device:

.. code-block::

    > ping fd14:c8eb:d14c:5fbe:b57e:1e02:a532:26d1

    16 bytes from fd14:c8eb:d14c:5fbe:b57e:1e02:a532:26d1: icmp_seq=1 hlim=255 time=562ms
    1 packets transmitted, 1 packets received. Packet loss = 0.0%. Round-trip min/avg/max = 562/562.0/562 ms.
    Done

