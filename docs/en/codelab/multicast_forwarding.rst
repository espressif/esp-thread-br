*************************
3.2. Multicast Forwarding
*************************

Multicast Forwarding allows reaching devices on the Thread and Wi-Fi network in the same multicast group from both sides.

Hardware Prerequisites
------------------------

To perform Multicast Forwarding, the following devices are required:

- An ESP Thread Border Router
- A Thread CLI device
- A Linux Host machine

The Border Router and the Linux Host machine shall be connected to the same Wi-Fi network and the Thread device shall join the Thread network formed by the Border Router.

Limits on Multicast Groups
--------------------------

Note that to forward packets between the Thread and the Wi-Fi network, the multicast group scope shall be at least admin-local(ff04). Link-local and realm-local multicast packets will not be forwarded.

Reaching the Multicast Group from the Wi-Fi Network via ICMP
------------------------------------------------------------

First, join the multicast group on the Thread CLI device:

.. code-block::

   mcast join ff04::123


Now you can ping Thread CLI device on your Linux Host:

.. code-block:: bash

   ping -I wlan0 -t 64 ff04::123


The output similar to shown below may indicate that the device can be reached on the Wi-Fi network via the multicast group:

.. code-block:: bash

   $ ping -I wlan0 -t 64 ff04::123
   PING ff04::123(ff04::123) from fdde:ad00:beef:cafe:2eea:7fff:fe37:b4fb wlan0: 56 data bytes
   64 bytes from fd66:afad:575f:1:744d:573e:6e60:188a: icmp_seq=1 ttl=254 time=132 ms


Reaching the Multicast Group from the Wi-Fi Network via UDP
------------------------------------------------------------

First, join the multicast group and create a UDP socket on the Thread CLI device:

.. code-block::

	> mcast join ff04::123
	Done
	> udp open
	Done
	> udp bind :: 5083
	Done


Use the following python script, which is named ``multicast_udp_client.py``, to send UDP messages to the Thread CLI device via the multicast group:

.. code-block:: python

	import socket
	import time

	data = b'hello'
	group = 'ff04::123'
	REMOTE_PORT = 5083
	network_interface = 'wlan0' # Change to the actual interface name

	sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
	sock.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, network_interface.encode())
	sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS, 32)
	sock.sendto(data, (group, REMOTE_PORT))
	sock.close()


On the Linux Host machine, you need to start a UDP client by running this script:

.. code-block:: bash

 	python3 multicast_udp_client.py


On the Thread CLI device, the message will be printed:

.. code-block::

	> mcast join ff04::123
	Done
	> udp open
	Done
	> udp bind :: 5083
	Done
	5 bytes from fd11:1111:1122:2222:4a9e:272e:6a50:cf61 56024 hello


Reaching the Multicast Group from the Thread Network
-----------------------------------------------------

Use the following python script, which is named ``multicast_udp_server.py``, to join an admin-local group and set up a UDP server on the Linux machine:

.. code-block:: python

	import socket
	import struct

	if_index = socket.if_nametoindex('wlan0')
	sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
	sock.bind(('::', 5083))
	sock.setsockopt(
		socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP,
		struct.pack('16si', socket.inet_pton(socket.AF_INET6, 'ff04::123'),
					if_index))
	while True:
		data, sender = sock.recvfrom(1024)
		print(data, sender)


On the Linux Host machine, you need to start a UDP server by running this script:

.. code-block:: bash

 	python3 multicast_udp_server.py


After launching the script and the Thread CLI device will be able to send UDP messages to the Linux Host via the multicast group:

.. code-block::

	udp open


.. code-block::

	udp send ff04::123 5083 hello


You will get a result ``Done`` after executing each of the commands, the expected output is below:

.. code-block::

	> udp open
	Done
	> udp send ff04::123 5083 hello
	Done

On the Linux Host machine, the message will be printed:

.. code-block:: bash

 	$ python3 multicast_udp_server.py
	b'hello' ('fd66:afad:575f:1:744d:573e:6e60:188a', 49154, 0, 0)
