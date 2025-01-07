*******************************************
3.6. Thread Radio Encapsulation Link (TREL)
*******************************************

TREL provides the mechanism for encapsulating IEEE 802.15.4 MAC frames
within UDP/IPv6 packets. This encapsulation allows Thread messages to
traverse IP-based networks while remaining compatible with existing Thread
devices, once the Thread messages are again de-encapsulated from the
UDP/IPv6 packet.

Hardware Prerequisites
----------------------

To perform Thread Radio Encapsulation Link (TREL), the following devices are required:

- An ESP Thread Border Router (BR)
- A Thread CLI device
- A TREL enabled Wi-Fi device

    - Note that this TREL Wi-Fi device does not need to have built-in 802.15.4 capabilities

The Border Router and the Wi-Fi device shall be connected to the same Wi-Fi network and 
the Thread CLI device shall join the Thread network formed by the Border Router:

1. Thread BR connects to Wi-Fi network, forms a Thread network, and assumes the role of LEADER
2. Thread CLI device joins Thread network formed by Thread BR
3. TREL Wi-Fi device connects to the same Wi-Fi network, and joins the Thread network created by the BR using Thread CLI

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

