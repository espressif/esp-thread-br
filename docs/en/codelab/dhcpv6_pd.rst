**********************************
3.8. DHCPv6 Prefix Delegation (PD)
**********************************

The ESP Thread Border Router can act as a DHCPv6 Prefix Delegation (PD) client to request an IPv6 prefix from a DHCPv6 server. Once a prefix is delegated, the Border Router can advertise this prefix to Thread devices, allowing them to obtain IPv6 addresses from the delegated prefix range.

Hardware Prerequisites
----------------------

To perform DHCPv6 Prefix Delegation, the following devices are required:

- An ESP Thread Border Router
- A Thread CLI device
- A Linux Host machine

The Border Router and the Linux Host machine shall be connected to the same Wi-Fi network and the Thread device shall join the Thread network formed by the Border Router.

Setting up Kea DHCPv6 Server
----------------------------

Install Kea DHCP Server on the Linux Host machine:

.. code-block:: bash

    sudo apt-get update
    sudo apt-get install kea-dhcp6-server

Configure Kea DHCPv6 Server to delegate a prefix. Create or edit the Kea configuration file (typically located at ``/etc/kea/kea-dhcp6.conf``), replacing `wlan0` with your network interface name:

.. code-block:: json

    {
        "Dhcp6": {
            "interfaces-config": {
                "interfaces": ["wlan0"]
            },
            "lease-database": {
                "type": "memfile",
                "persist": true,
                "name": "/var/lib/kea/dhcp6.leases"
            },
            "preferred-lifetime": 3000,
            "valid-lifetime": 4000,
            "subnet6": [
                {
                    "subnet": "2001:db8::/64",
                    "pools": [],
                    "pd-pools": [
                        {
                            "prefix": "2001:db8:1::",
                            "prefix-len": 48,
                            "delegated-len": 64
                        }
                    ]
                }
            ]
        }
    }

Start the Kea DHCPv6 server:

.. code-block:: bash

    sudo systemctl start kea-dhcp6-server
    sudo systemctl enable kea-dhcp6-server

Verify that the Kea DHCPv6 server is running:

.. code-block:: bash

    sudo systemctl status kea-dhcp6-server

Configuring the Border Router
-----------------------------

Configure the ESP Thread Border Router to act as a DHCPv6 PD client. This can be done at runtime using CLI commands:

.. code-block::

    > br pd enable
    Done

Verify that the Border Router has requested and received a prefix delegation:

.. code-block::

    > br pd state
    running
    Done

    > br pd omrprefix
    2001:db8:1:3::/64 lifetime:3120 preferred:3000
    Done


Verifying IPv6 Address Assignment
---------------------------------

Once the Border Router has received a delegated prefix, it will advertise this prefix to Thread devices. The Thread CLI device should automatically obtain an IPv6 address from the delegated prefix range.

Check the IPv6 addresses on the Thread CLI device:

.. code-block::

    esp32c6> ot ipaddr
    2001:db8:1:3:eae1:e8ce:1f85:d1dd
    fd5b:281f:1ef6:923c:0:ff:fe00:d001
    fd5b:281f:1ef6:923c:37b4:c051:fe3d:41b3
    fe80:0:0:0:5059:8382:b52:5f0
    Done


The output should show an IPv6 address that belongs to the delegated prefix range (e.g., ``2001:db8:1::xxxx:xxxx:xxxx:xxxx`` if the delegated prefix is ``2001:db8:1::/64``).

Verify connectivity from the Linux Host machine:

.. code-block:: bash

    ~ ping 2001:db8:1:3:eae1:e8ce:1f85:d1dd
    PING 2001:db8:1:3:eae1:e8ce:1f85:d1dd(2001:db8:1:3:eae1:e8ce:1f85:d1dd) 56 data bytes
    64 bytes from 2001:db8:1:3:eae1:e8ce:1f85:d1dd: icmp_seq=1 ttl=254 time=1734 ms
    64 bytes from 2001:db8:1:3:eae1:e8ce:1f85:d1dd: icmp_seq=2 ttl=254 time=682 ms
    64 bytes from 2001:db8:1:3:eae1:e8ce:1f85:d1dd: icmp_seq=3 ttl=254 time=297 ms
    64 bytes from 2001:db8:1:3:eae1:e8ce:1f85:d1dd: icmp_seq=4 ttl=254 time=424 ms
    64 bytes from 2001:db8:1:3:eae1:e8ce:1f85:d1dd: icmp_seq=5 ttl=254 time=73.3 ms
    ^C
    --- 2001:db8:1:3:eae1:e8ce:1f85:d1dd ping statistics ---
    5 packets transmitted, 5 received, 0% packet loss, time 4052ms
    rtt min/avg/max/mdev = 73.342/642.272/1733.868/580.221 ms, pipe 2

You should see successful ping responses, confirming that the Thread device has received an IPv6 address from the delegated prefix and is reachable.
