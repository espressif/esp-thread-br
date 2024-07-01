************************************
2.2. Setting up the Local OTA Server
************************************

This document contains instructions on setting up a self-signed HTTPS OTA server on the local host and configuring the Border Router to trust the server.


Generating the Server Certificate
----------------------------------

Create a new directory ``ota_server_storage`` to host the server. Then generate the certificate in this directory:

.. code-block:: bash

   mkdir ota_server_storage && cd ota_server_storage


.. code-block:: bash

   openssl req -x509 -newkey rsa:2048 -keyout ca_key.pem -out ca_cert.pem -days 365 -nodes


Note that when prompted for the ``Common Name (CN)``, the name entered shall match the hostname running the server.

The Border Router OTA image will be automatically generated when building the ``basic_thread_border_router`` example. Copy it to the previously created directory ``ota_server_storage``:

.. code-block:: bash

   cp esp-thread-br/examples/basic_thread_border_router/build/ota_with_rcp_image ota_server_storage

Rebuilding the Border Router Firmware with the New Certificate
---------------------------------------------------------------

The certificate ``server_certs/ca_cert.pem`` in the ``basic_thread_border_router`` example shall be replaced with the generated certificate. Perform a fullclean and build the application again:

.. code-block:: bash

   cp path/to/ota_server_storage/ca_cert.pem server_certs/ca_cert.pem


.. code-block:: bash

   idf.py fullclean


.. code-block:: bash

   idf.py flash monitor


To download the image from the server and run the following command on the Border Router:

.. code-block:: bash

   ota download https://${HOST_URL}:8070/ota_with_rcp_image

