*************************************
3.1. Bi-directional IPv6 Connectivity
*************************************

The ESP Thread Border Router allows the devices on the Wi-Fi/Ethernet and the Thread network to reach each other with IPv6 addresses.

Hardware Prerequisites
------------------------

To perform bi-directional IPv6 connectivity, the following devices are required:

- An ESP Thread Border Router
- A Thread CLI device
- A Linux Host machine

The Border Router and the Linux Host machine shall be connected to the same Wi-Fi network and the Thread device shall join the Thread network formed by the Border Router.


Validate the IPv6 Connectivity
-------------------------------

The Linux Host machine needs to be configured to accept the router advertisements from the Border Router:

Setting ``accept_ra`` to 2 allows all RAs to be accepted:

.. code-block:: bash

    sudo sysctl -w net/ipv6/conf/wlan0/accept_ra=2


Setting ``accept_ra_rt_info_max_plen`` to 128 allows all kinds of prefix length in RAs to be accepted:

.. code-block:: bash

    sudo sysctl -w net/ipv6/conf/wlan0/accept_ra_rt_info_max_plen=128


A global address will be assigned to the Thread CLI device. The Linux Host machine can reach the Thread device with an address that can be obtained by running the command `ipaddr` on the CLI device.

.. code-block::

    ipaddr


The command would produce the similar output:

.. code-block::

    > ipaddr
    fd66:afad:575f:1:744d:573e:6e60:188a
    fd87:8205:4651:27c8:0:ff:fe00:0
    fd87:8205:4651:27c8:e65a:3138:745a:df06
    fe80:0:0:0:2433:db2e:62c:b2e4
    Done


The Linux Host reachable address is ``fd66:afad:575f:1:744d:573e:6e60:188a``, you can ping this address from Linux Host using the following command:

.. code-block:: bash

    ping fd66:afad:575f:1:744d:573e:6e60:188a


You well get these output:

.. code-block:: bash

    $ ping fd66:afad:575f:1:744d:573e:6e60:188a

    PING fd66:afad:575f:1:744d:573e:6e60:188a(fd66:afad:575f:1:744d:573e:6e60:188a) 56 data bytes
    64 bytes from fd66:afad:575f:1:744d:573e:6e60:188a: icmp_seq=1 ttl=254 time=187 ms
    64 bytes from fd66:afad:575f:1:744d:573e:6e60:188a: icmp_seq=2 ttl=254 time=167 ms
