**********************
3.3. Service Discovery
**********************

The ESP Thread Border Router allows devices on the Thread and Wi-Fi/Ethernet network to discover services published on both the networks.

Hardware Prerequisites
------------------------

To perform service discovery, the following devices are required:

- An ESP Thread Border Router
- A Thread CLI device
- A Linux Host machine

The Border Router and the Linux Host machine shall be connected to the same Wi-Fi network and the Thread device shall join the Thread network formed by the Border Router.

Publishing Services from the Thread Network
--------------------------------------------

Thread uses the Service Registration Protocol (SRP) to register services. First, publish the service on the Thread CLI device.

Using this command line to set host name:

.. code-block::

   srp client host name thread-device


Using this command line to set host address, you can set default address with ``auto``:

.. code-block::

   srp client host address auto


Using this command line to set parameter of the service:

.. code-block::

   srp client service add thread-service _test._tcp,_sub1,_sub2 12345 1 1 0778797a3d58595a


Using this command line to enable the service:

.. code-block::

    srp client autostart enable


You will get a result ``Done`` after executing each of the commands, the expected output is below:

.. code-block::

   > srp client host name thread-device
   Done
   > srp client host address auto
   Done
   > srp client service add thread-service _test._tcp,_sub1,_sub2 12345 1 1 0778797a3d58595a
   Done
   > srp client autostart enable
   Done


You can execute this command on the Border Router to resolve the service:

.. code-block::

    srp server service


The service can be found on the Border Router:

.. code-block::

    > srp server service
    thread-service._test._tcp.default.service.arpa.
        deleted: false
        subtypes: _sub2,_sub1
        port: 12345
        priority: 1
        weight: 1
        ttl: 7200
        TXT: [xyz=58595a]
        host: thread-device.default.service.arpa.
        addresses: [fd66:afad:575f:1:744d:573e:6e60:188a]
    Done


The service can also be found on the Wi-Fi with the Linux Host machine by using this command:

.. code-block:: bash

    avahi-browse -rt _test._tcp


The expected output is below:

.. code-block:: bash

    $ avahi-browse -rt _test._tcp
    + enp1s0 IPv6 thread-service                                _test._tcp           local
    + enp1s0 IPv4 thread-service                                _test._tcp           local
    = enp1s0 IPv6 thread-service                                _test._tcp           local
       hostname = [thread-device.local]
       address = [fd66:afad:575f:1:744d:573e:6e60:188a]
       port = [12345]
       txt = ["xyz=XYZ"]
    = enp1s0 IPv4 thread-service                                _test._tcp           local
       hostname = [thread-device.local]
       address = [fd66:afad:575f:1:744d:573e:6e60:188a]
       port = [12345]
       txt = ["xyz=XYZ"]


Publishing Services from the Wi-Fi Network
------------------------------------------

First publish the service on the Linux Host machine with mDNS:

.. code-block:: bash

    avahi-publish-service wifi-service _test._tcp 22222 test=1 dn="aabbbb"


If the service is established, you will get this output on your Linux Host machine:

.. code-block:: bash

    $ avahi-publish-service wifi-service _test._tcp 22222 test=1 dn="aabbbb"
    Established under name 'wifi-service'


Then get the Border Router's Mesh-Local Endpoint Identifier, and configure it on the Thread end device. On the Border Router:

.. code-block::

    ipaddr mleid


You will get: 

.. code-block::

    > ipaddr mleid
    fdde:ad00:beef:0:f891:287:866:776
    Done

On the Thread CLI device:

.. code-block::

    dns config fdde:ad00:beef:0:f891:287:866:776


You will get:

.. code-block::

    > dns config fd9b:347f:93f7:1:1003:8f00:bcc1:3038
    Done


The service can be resolved on the Thread CLI device by executing this command:

.. code-block::

    dns service wifi-service _test._tcp.default.service.arpa.


The expected output on the Thread CLI device is below:

.. code-block::

    > dns config fdde:ad00:beef:0:f891:287:866:776
    Done
    > dns service wifi-service _test._tcp.default.service.arpa.
    DNS service resolution response for wifi-service for service _test._tcp.default.service.arpa.
    Port:22222, Priority:0, Weight:0, TTL:120
    Host:FA001388.default.service.arpa.
    HostAddress:fd33:1cc4:a6ec:2e0:2eea:7fff:fe37:b4fb TTL:120
    TXT:[test=31, dn=616162626262] TTL:120

    Done

