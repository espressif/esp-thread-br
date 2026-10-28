************
3.5. WEB GUI
************

The ESP Thread Border Router provides a Web GUI to discover, configure, and monitor Thread networks.

To access the Web GUI, enter ``http://esp-ot-br.local`` in your browser window.

Prerequisites
-------------

To use the Web GUI, you need:

- An ESP Thread Border Router
- A computer or mobile device with a web browser

Enable the ``CONFIG_OPENTHREAD_BR_START_WEB`` option to enable the Web Server feature.

Connect the browser device to the same local network as the Thread Border Router.

When the ESP Thread Border Router starts up, it prints the Web GUI address to the serial console. For example:

.. code-block::

    otbr_web: <========server start========>

    otbr_web: http://esp-ot-br.local

    otbr_web: <============================>


All REST APIs can be accessed by visiting ``http://esp-ot-br.local`` with the API path specified.

Thread REST APIs
----------------

REST API version 2.0.0 follows the methods, JSON schemas, and action model of
``ot-br-posix`` REST API 0.6.0. All endpoints below use the ESP
HTTP server on port 80; no separate server on port 8081 is needed.

.. list-table:: Supported Thread REST endpoints
   :header-rows: 1
   :widths: 65 35

   * - Path
     - Methods
   * - ``/.well-known/thread/br-rest``
     - GET, OPTIONS
   * - ``/node``
     - GET, DELETE, OPTIONS
   * - ``/node/ba-id``, ``/node/rloc``, ``/node/rloc16``, ``/node/ext-address``
     - GET, OPTIONS
   * - ``/node/network-name``, ``/node/leader-data``, ``/node/num-of-router``, ``/node/ext-panid``
     - GET, OPTIONS
   * - ``/node/state``
     - GET, PUT, OPTIONS
   * - ``/node/dataset/active``, ``/node/dataset/pending``
     - GET, PUT, OPTIONS
   * - ``/node/commissioner/state``
     - GET, PUT, OPTIONS
   * - ``/node/commissioner/joiner``
     - GET, POST, DELETE, OPTIONS
   * - ``/node/coprocessor/version``
     - GET, OPTIONS
   * - ``/node/ba-epskc/state``
     - GET, PUT, OPTIONS
   * - ``/node/ba-epskc/key``
     - GET, POST, DELETE, OPTIONS
   * - ``/api/node``
     - GET, OPTIONS
   * - ``/api/actions``
     - GET, POST, DELETE, OPTIONS
   * - ``/api/actions/{id}``
     - GET, DELETE, OPTIONS
   * - ``/api/devices``, ``/api/devices/{id}``
     - GET, DELETE, OPTIONS
   * - ``/api/diagnostics``, ``/api/diagnostics/{id}``
     - GET, DELETE, OPTIONS

Visit `openapi.yaml <https://github.com/espressif/esp-thread-br/blob/main/components/esp_ot_br_server/src/openapi.yaml>`_
for request bodies, response schemas, media types, and error codes.
Commissioner support (``CONFIG_OPENTHREAD_COMMISSIONER``) is required for
commissioning and energy scans. The firmware must include OpenThread mesh
diagnostics and Border Agent ephemeral key support.

For example, ``GET http://esp-ot-br.local/node`` returns:

.. code-block:: json

    {
        "baId": "00112233445566778899aabbccddeeff",
        "baState": "",
        "state": "leader",
        "routerCount": 1,
        "rlocAddress": "fd12:cb40:859f:287e:0:ff:fe00:3800",
        "extAddress": "caf97e6ee990b047",
        "networkName": "OpenThread-4c68",
        "rloc16": "0x3800",
        "routerId": 14,
        "extPanId": "f4f9437404558d34",
        "leaderData": {
            "partitionId": 61563841,
            "weighting": 64,
            "dataVersion": 211,
            "stableDataVersion": 110,
            "leaderRouterId": 14
        }
    }

Individual scalar endpoints retain their scalar formats: for example,
``/node/state`` returns a JSON string and ``/node/rloc16`` returns a JSON number.
PUT ``/node/state`` accepts the JSON strings ``"enable"`` and ``"disable"``.
DELETE ``/node`` erases persistent Thread settings, sends the HTTP response,
and then restarts the ESP device to complete the factory reset.

Use ``Accept: application/json`` for structured datasets, or
``Accept: text/plain`` for hex-encoded dataset TLVs. Set the matching
``Content-Type`` when writing a dataset. Active datasets can be written while
Thread is disabled. Pending datasets require an attached node and are registered
with the leader using MGMT_PENDING_SET. ``If-None-Match: *`` makes either write
conditional on the corresponding dataset being absent; an existing dataset
returns HTTP 412. A pending write can wait up to 30 seconds for the leader's
response before returning HTTP 504. The HTTP server handles requests serially,
so other HTTP requests may wait during that operation.

The ``/api/*`` collections support ``application/vnd.api+json`` and
``application/json``, with the same media-type rules and ``fields[type]`` query
parameters as POSIX. Actions run asynchronously and can be polled by ID.
Supported action types are ``addThreadDeviceTask``, ``getEnergyScanTask``,
``getNetworkDiagnosticTask``, ``resetNetworkDiagCounterTask``, and
``updateDeviceCollectionTask``. For example:

.. code-block:: shell

    curl -H 'Accept: application/vnd.api+json' http://esp-ot-br.local/api/actions
    curl http://esp-ot-br.local/node/coprocessor/version
    curl -X POST -H 'Content-Type: application/vnd.api+json' \
      -d '{"data":[{"type":"updateDeviceCollectionTask","attributes":{"maxAge":1.5,"maxRetries":3,"deviceCount":5,"timeout":5}}]}' \
      http://esp-ot-br.local/api/actions

Collections live in RAM and are cleared on reboot. HTTP request bodies are
limited to 16 KiB; larger requests return HTTP 413. Each collection holds up to 200 items. Provision enough heap (PSRAM is recommended)
for the network size and diagnostic history, and synchronize the system clock
if collection timestamps are used externally.

Migration from API 1.x
^^^^^^^^^^^^^^^^^^^^^^

Structured Thread REST objects now use lower camel case, including nested
leader data and dataset fields. For example, ``NetworkName`` becomes
``networkName``, ``NumOfRouter`` becomes ``routerCount``, and numeric ``State``
becomes the role string ``state``. Update clients that consumed the old
structured responses or sent datasets with capitalized keys.

Clients should discover the API at ``/.well-known/thread/br-rest`` and use
``/api/diagnostics`` for collection access.


Web GUI REST APIs
-----------------

The reference Web GUI uses the following APIs. Responses contain ``error``,
``result``, and ``message`` fields. POST requests use ``application/json``.

.. list-table:: Web GUI endpoints
   :header-rows: 1
   :widths: 15 30 55

   * - Method
     - Path
     - Function
   * - GET
     - ``/get_properties``
     - Read network status, identifiers, firmware versions, uptime, and free heap.
   * - GET
     - ``/available_network``
     - Scan for nearby Thread networks.
   * - POST
     - ``/join_network``
     - Join a scanned network using a network key or PSKd.
   * - POST
     - ``/form_network``
     - Form a Thread network with the supplied parameters.
   * - POST
     - ``/commission``
     - Start the commissioner and configure the supplied joiner PSKd.
   * - GET
     - ``/ipaddr``
     - List Thread interface IPv6 addresses, their origins, and preferred status.
   * - POST
     - ``/add_ipaddr``, ``/delete_ipaddr``
     - Add or remove an IPv6 address.
   * - POST
     - ``/add_prefix``, ``/delete_prefix``
     - Add or remove an on-mesh prefix.
   * - POST
     - ``/ping``
     - Send ICMPv6 echo requests and return replies and summary statistics.
   * - GET
     - ``/node_information``
     - Read the border router's Thread node information.
   * - GET
     - ``/topology``
     - Collect router diagnostics and child table entries for the topology view.

The GUI also uses ``GET /node/state`` from the Thread REST APIs to check
attachment and monitor network joining and formation.

The ``/available_network`` API returns scan results, for example:

Open this URL in a browser:

.. code-block::

    http://esp-ot-br.local/available_network


The feedback result may appear as follows:

.. code-block::

    {
        "error":	0,
        "result":	[{
                "id":	1,
                "nn":	"OpenThread",
                "ep":	"dead00beef00cafe",
                "pi":	"0xa06d",
                "ha":	"5a1ee78f873814fc",
                "ch":	11,
                "ri":	-35,
                "li":	229
            }, {
                "id":	2,
                "nn":	"GRL",
                "ep":	"000db80000000000",
                "pi":	"0xfacf",
                "ha":	"166e0a0000000003",
                "ch":	17,
                "ri":	-70,
                "li":	51
            }, {
                "id":	3,
                "nn":	"NEST-PAN-3DDF",
                "ep":	"4500ddd4f9c1597d",
                "pi":	"0x3ddf",
                "ha":	"9e517ed148e81409",
                "ch":	20,
                "ri":	-39,
                "li":	209
            }],
        "message":	"Networks: Success"
    }


The web server of ESP Thread Border Router provides the ``get_properties`` API to check the Thread network status.

Open this URL in a browser:

.. code-block::

    http://esp-ot-br.local/get_properties


The feedback result may appear as follows:

.. code-block::

    {
        "error":	0,
        "result":	{
            "IPv6:LinkLocalAddress":	"fe80:0:0:0:c8f9:7e6e:e990:b047",
            "IPv6:RoutingLocalAddress":	"fd12:cb40:859f:287e:0:ff:fe00:3800",
            "IPv6:MeshLocalAddress":	"fd12:cb40:859f:287e:a8b5:c617:396b:a4c2",
            "IPv6:MeshLocalPrefix":	"fd12:cb40:859f:287e::/64",
            "Network:Name":	"OpenThread-4c68",
            "Network:PANID":	"0x1254",
            "Network:PartitionID":	"61563841",
            "Network:XPANID":	"f4f9437404558d34",
            "OpenThread:Version":	"openthread-esp32/f4446d8819-091f68ed7; esp32s3;  2023-05-05 13:05:02 UTC",
            "OpenThread:Version API":	"292",
            "Thread:Role":	"leader",
            "Network:BorderAgentID":	"00112233445566778899aabbccddeeff",
            "IDF:Version":	"v5.5.4",
            "App:Version":	"1.0",
            "App:ProjectName":	"esp_ot_br",
            "App:CompileTime":	"Oct 09 2026 10:00:00",
            "System:Uptime":	"00:10:00",
            "System:FreeHeap":	"100 KB",
            "OpenThread:PSKc":	"e66d93364793c33985280abb639c214c",
            "RCP:Channel":	"12",
            "RCP:EUI64":	"6055f9f72eebfeff",
            "RCP:TxPower":	"10 dBm",
            "RCP:Version":	"openthread-esp32/f4446d8819-091f68ed7; esp32h2;  2023-05-04 08:35:37 UTC",
            "WPAN service":	"associated"
        },
        "message":	"Properties: Success"
    }


The web server of ESP Thread Border Router provides the ``node_information`` API to obtain the Thread node information.

Open this URL in a browser:

.. code-block::

    http://esp-ot-br.local/node_information


The feedback result may appear as follows:

.. code-block::

    {
        "error":	0,
        "result":	{
            "NetworkName":	"OpenThread-4c68",
            "ExtPanId":	"f4f9437404558d34",
            "ExtAddress":	"caf97e6ee990b047",
            "RlocAddress":	"fd12:cb40:859f:287e:0:ff:fe00:3800",
            "LeaderData":	{
                "PartitionId":	61563841,
                "Weighting":	64,
                "DataVersion":	225,
                "StableDataVersion":	124,
                "LeaderRouterId":	14
            },
            "State":	4,
            "Rloc16":	14336,
            "NumOfRouter":	1
        },
        "message":	"Get Node: Success"
    }

The ``/topology`` API returns router diagnostics and child table entries from the current Thread network.

Open this URL in a browser:

.. code-block::

    http://esp-ot-br.local/topology


The feedback result may appear as follows:

.. code-block:: json

    {
        "error": 0,
        "result": [
            {
                "ExtAddress": "caf97e6ee990b047",
                "Rloc16": 14336,
                "Mode": {
                    "RxOnWhenIdle": 1,
                    "DeviceType": 1,
                    "NetworkData": 1
                },
                "Route": {
                    "IdSequence": 131,
                    "RouteData": [
                        {
                            "RouteId": 14,
                            "LinkQualityOut": 0,
                            "LinkQualityIn": 0,
                            "RouteCost": 1
                        }
                    ]
                },
                "LeaderData": {
                    "PartitionId": 61563841,
                    "Weighting": 64,
                    "DataVersion": 229,
                    "StableDataVersion": 128,
                    "LeaderRouterId": 14
                },
                "IP6AddressList": [
                    "fd12:cb40:859f:287e:0:ff:fe00:3800",
                    "fd12:cb40:859f:287e:a8b5:c617:396b:a4c2"
                ],
                "ChildTable": []
            }
        ],
        "message": "Topology: Success"
    }


The web server provides a ``HTTP_POST`` entry that allows users to configure the Border Router to use either ``networkKeyType`` or ``pskdType`` for joining other networks.

The JSON format of ``join_network`` API appears as follow:

.. code-block::

    {
        "credentialType":   "networkKeyType",
        "networkKey"    :   "00112233445566778899aabbccddeeff",
        "pskd"          :   "",
        "index"         :   1
    }


Note that the network to be joined MUST be the networks scanned by the ``available_network`` API, the ``index`` indicates the sequence of available networks.

The web server provides a ``HTTP_POST`` entry that allows users to configure the Border Router to use the parameter provided by user for forming a Thread network.

The JSON format of ``form_network`` API appears as follow:

.. code-block::

    {
        "networkName"   :   "OpenThread-0x99",
        "networkKey"    :   "00112233445566778899aabbccddeeff",
        "panId"         :   "0x1234",
        "channel"       :   16,
        "extPanId"      :   "1111111122222222",
        "passphrase"    :   "j01Nme",
        "prefix"        :   "fd11:22::",
        "defaultRoute"  :   true
    }


The web server provides a ``HTTP_POST`` entry that allows users to configure the Border Router for setting current Thread network.

The JSON format of ``add_prefix`` API appears as follow:

.. code-block::

    {
        "prefix":       "fd11:22::",
        "defaultRoute":  true
    }


The JSON format of ``delete_prefix`` API appears as follow:

.. code-block::

    {
        "prefix":       "fd11:22::"
    }


Additional Web GUI POST request examples:

.. code-block:: json

    {"pskd": "J01NME"}

Use this body with ``/commission`` to supply a joiner PSKd.

.. code-block:: json

    {"address": "2001:db8::1"}

Use this body with ``/add_ipaddr`` or ``/delete_ipaddr``. The Addresses page
provides a Remove button for manually added addresses.

.. code-block:: json

    {"address": "fd11:22::1", "count": 4, "size": 32}

Use this body with ``/ping``. Its result contains ``replies`` and ``statistics``,
including packet counts, packet loss, and round-trip times.


Web GUI Application Introduction
---------------------------------

The SDK provides a Web GUI implementation as a reference. Customers can customize
the Web GUI or build their own interface using the REST APIs documented above.

The Web GUI provides the following pages:

- **Dashboard**: View Thread network status, IPv6 addresses, uptime, free heap, and device identifiers.
- **Network**: Scan for Thread networks, join using a network key or PSKd, or form a new network with configurable parameters.
- **Commissioner**: Start Thread commissioning with a supplied joiner PSKd.
- **Addresses**: List IPv6 addresses, add or remove manual addresses, and add or remove on-mesh prefixes.
- **Tools**: Send IPv6 pings with configurable count and payload size, and view replies, packet loss, and round-trip times.
- **Topology**: Display routers and children, inspect node details, and ping a selected node.
- **About**: View application, ESP-IDF, OpenThread, and radio coprocessor versions, and related resource links.

Network, Commissioner, Addresses, and Tools are grouped under the Management
menu. Commissioner, Addresses, Tools, and Topology controls require the border
router to be attached to a Thread network.

With ``CONFIG_OPENTHREAD_BR_SOFTAP_SETUP`` enabled, a separate Wi-Fi setup portal
is available in SoftAP setup mode to scan Wi-Fi networks and save connection
credentials.
