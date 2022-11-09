# Openthread Extension Commands

The ESP OpenThread examples provide a series of extension commands in addition to the standard [OpenThread CLI](https://github.com/openthread/openthread/blob/main/src/cli/README.md).

## Enabling the extension commands

To enable OpenThread extension commands, the following Kconfig option needs to be enabled:
`OpenThread Extension CLI` -> `Enable Espressif's extended features`.

## Commands

* [iperf](#iperf)
* [tcpsockclient](#tcpsockclient)
* [tcpsockserver](#tcpsockserver)
* [udpsockclient](#udpsockclient)
* [udpsockserver](#udpsockserver)
* [wifi](#wifi)
* [ota](#ota)
* [dns64server](#dns64server)
* [curl](#curl)

### iperf

Iperf is a tool for performing TCP or UDP throughput on the Thread network.

For running iperf, you need to have two Thread devices on the same network.

* General Options

```bash
iperf
---iperf parameter---
-s                  :     server mode, only receive
-u                  :     upd mode
-V                  :     use IPV6 address
-c <addr>           :     client mode, only transmit
-i <interval>       :     seconds between periodic bandwidth reports
-t <time>           :     time in seconds to transmit for (default 10 secs)
-p <port>           :     server port to listen on/connect to
-l <len_send_buf>   :     the length of send buffer
---example---
create a tcp server :     iperf -s -i 3 -p 5001 -t 60
create a udp client :     iperf -c <addr> -u -i 3 -t 60 -p 5001 -l 512
Done
```

* Typical usage

For measuring the TCP throughput, first create an iperf service on one node:
```bash
> iperf -V -s -t 20 -i 3 -p 5001
Done
```

Then create an iperf client connecting to the service on another node. Here we use `fdde:ad00:beef:0:a7c6:6311:9c8c:271b` as the example service address.

```bash
> iperf -V -c fdde:ad00:beef:0:a7c6:6311:9c8c:271b -t 20 -i 1 -p 5001 -l 85
Done
        Interval Bandwidth
   0-   1 sec       0.05 Mbits/sec
   1-   2 sec       0.05 Mbits/sec
   2-   3 sec       0.05 Mbits/sec
   3-   4 sec       0.05 Mbits/sec
   4-   5 sec       0.05 Mbits/sec
...
   19-   20 sec       0.05 Mbits/sec
   0-   20 sec       0.05 Mbits/sec
```

For measuring the UDP throughput, first create an iperf service similarly:

```bash
> iperf -V -u -s -t 20 -i 3 -p 5001
Done
```

Then create an iperf client:

```bash
> iperf -V -u -c fdde:ad00:beef:0:a7c6:6311:9c8c:271b -t 20 -i 1 -p 5001 -l 85
Done
```

### tcpsockserver

Used for creating a tcp server.

* General Options

```bash
> tcpsockserver
---tcpsockserver parameter---
status                     :     get tcp server status
open                       :     open tcp server function
bind <ipaddr> <port>       :     create a tcp server with binding the ipaddr and port
send <message>             :     send a message to the tcp client
close                      :     close tcp server
---example---
get tcp server status      :     tcpsockserver status
open tcp server function   :     tcpsockserver open
create a tcp server        :     tcpsockserver bind :: 12345
send a message             :     tcpsockserver send hello
close tcp server           :     tcpsockserver close
Done
```

* Typical usage

Open the tcp server function.

```bash
> tcpsockserver open
Done
```

Create a tcp server with binding.

```bash
> tcpsockserver bind :: 12345
Done
I (362245) ot_socket: Socket created
I (362245) ot_socket: Socket bound, port 12345
I (362245) ot_socket: Socket listening
I (362255) ot_socket: Successfully created
```

Now the tcp client can connect the tcp server.

```bash
I (448575) ot_socket: Socket accepted ip address: FDDE:AD00:BEEF:CAFE:F612:FAFF:FE40:37A0
```

Check the status of tcp client

```bash
> tcpsockserver status
connected       remote ipaddr: FDDE:AD00:BEEF:CAFE:F612:FAFF:FE40:37A0
Done
```

Send a message to the tcp client.

```bash
> tcpsockserver send hello
Done
```

Receive a message from the tcp client.

```bash
I (635835) ot_socket: sock 55 Received 5 bytes from FDDE:AD00:BEEF:CAFE:F612:FAFF:FE40:37A0
I (635835) ot_socket: hello
```

Close the tcp server.

```bash
> tcpsockserver close
Done
I (232650) ot_socket: TCP server is disconnecting with FDDE:AD00:BEEF:CAFE:F612:FAFF:FE40:37A0
I (232650) ot_socket: TCP server receive task exiting
I (232660) ot_socket: Closed tcp server successfully
```

### tcpsockclient

Used for creating a tcp client.

* General Options

```bash
> tcpsockclient
---tcpsockclient parameter---
status                     :     get tcp client status
open                       :     open tcp client function
connect <ipaddr> <port>    :     create a tcp client and connect the server
send <message>             :     send a message to the tcp server
close                      :     close tcp client 
---example---
get tcp client status      :     tcpsockclient status
open tcp client function   :     tcpsockclient open
create a tcp client        :     tcpsockclient connect fd81:984a:b59d:2::c0a8:0166 12345
send a message             :     tcpsockclient send hello
close tcp client           :     tcpsockclient close
Done
```

* Typical usage

Open the tcp client function.

```bash
> tcpsockclient open
Done
```

Create a tcp client and connect the tcp server

```bash
> tcpsockclient connect fd0d:e86e:4ac3:1:81f3:d614:e2ec:46ec 12345
Done
I (164956) ot_socket: Socket created, connecting to FD0D:E86E:4AC3:1:81F3:D614:E2EC:46EC:12345
I (165126) ot_socket: Successfully connected
```

Check the status of tcp client

```bash
> tcpsockclient status
connected       remote ipaddr: FD0D:E86E:4AC3:1:81F3:D614:E2EC:46EC
Done
```

Send a message to the tcp server.

```bash
> tcpsockclient send hello
Done
```

Receive a message from the tcp server.

```bash
I (270416) ot_socket: sock 54 Received 5 bytes from FD0D:E86E:4AC3:1:81F3:D614:E2EC:46EC
I (270426) ot_socket: hello
```

Close the tcp client.

```bash
> tcpsockclient close
Done
I (307796) ot_socket: TCP client is disconnecting with FD0D:E86E:4AC3:1:81F3:D614:E2EC:46EC
I (307796) ot_socket: TCP client receive task exiting
I (307796) ot_socket: Closed tcp client successfully
```

### udpsockserver

Used for creating a udp server.

```bash
> udpsockserver
Done
I (1310225) ot_socket: Socket created
I (1310225) ot_socket: Socket bound, port 12345
I (1310225) ot_socket: Socket listening, timeout is 30 seconds
```

### udpsockclient

Used for creating a udp client. Note that the client shall be connected to the same Thread network as the server.

```bash
> udpsockclient fdde:ad00:beef:0:a7c6:6311:9c8c:271b
Done
ot_socket: Socket created, connecting to fdde:ad00:beef:0:a7c6:6311:9c8c:271b:12345
ot_socket: Successfully connected
...
```

### wifi

Used for connecting the border router to the Wi-Fi network.

```bash
> wifi
--wifi parameter---
connect
-s                   :     wifi ssid
-p                   :     wifi psk
---example---
join a wifi:
ssid: threadcertAP
psk: threadcertAP    :     wifi connect -s threadcertAP -p threadcertAP
state                :     get wifi state, disconnect or connect
---example---
get wifi state       :     wifi state
Done
```

To join a Wi-Fi network, please use the `wifi connect` command:

```bash
> wifi connect -s threadcertAP -p threadcertAP
```

To get the state of the Wi-Fi network:

```bash
> wifi state
connected
Done
```

### ota

Used for downloading border router firmware and updating the border router or the RCP alone.

```
> ota download https://192.168.1.2:8070/br_ota_image
```

After downloading the device will restart and update itself with the new firmware. The RCP will also be updated if the firmware version changes.

```
> ota rcpudate
```

This command will enforce a RCP update regardless of the RCP version.

### dns64server

Used for setting the dns64 server. Note that the border router must support NAT64.

```
> dns64server 8.8.8.8
```

### curl

Used for fetching the content of a HTTP web page. Note that the border router must support NAT64.

```
> curl http://www.espressif.com
Done
<html>
<head><title>301 Moved Permanently</title></head>
<body bgcolor="white">
<center><h1>301 Moved Permanently</h1></center>
<hr><center>CloudFront</center>
</body>
</html>
```
