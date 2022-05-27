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

```bash
> tcpsockserver
Done
I (1310225) ot_socket: Socket created
I (1310225) ot_socket: Socket bound, port 12345
I (1310225) ot_socket: Socket listening, timeout is 30 seconds
```

### tcpsockclient

Used for creating a tcp client.

```bash
> tcpsockclient fdde:ad00:beef:0:a7c6:6311:9c8c:271b
Done
ot_socket: Socket created, connecting to fdde:ad00:beef:0:a7c6:6311:9c8c:271b:12345
ot_socket: Successfully connected
...
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
