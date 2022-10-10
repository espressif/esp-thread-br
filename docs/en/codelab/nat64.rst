**********
3.4. NAT64
**********

The ESP Thread Border Router supports NAT64 which allows Thread devices to visit the IPv4 Internet.

Hardware Prerequisites
------------------------

To perform NAT64, the following devices are required:

- An ESP Thread Border Router
- A Thread CLI device

The Thread device shall join the Thread network formed by the Border Router.

Visiting the IPv4 HTTP Servers
-------------------------------

For visiting HTTP servers with domain names, the DNS server shall be first configured on the Thread CLI device:

.. code-block:: bash

    dns64server 8.8.8.8


Then you can use ``curl <website>`` command to get the data form the specific website(for example ``http://www.espressif.com``):

.. code-block:: bash

    curl http://www.espressif.com


The Thread device will first resolve the host with UDP packets sent to the IPv4 DNS server. Then retrieve the page from the IPv4 HTTP server via TCP. The expected output is below:

.. code-block:: bash

    > dns64server 8.8.8.8
    Done
    > curl http://www.espressif.com
    Done
    > I (22289) HTTP_CLIENT: Body received in fetch header state, 0x3fcca6b7, 183
    <html>
    <head><title>301 Moved Permanently</title></head>
    <body bgcolor="white">
    <center><h1>301 Moved Permanently</h1></center>
    <hr><center>CloudFront</center>
    </body>
    </html>

