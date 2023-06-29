# Openthread Extension Commands

The ESP OpenThread examples provide a series of extension commands in addition to the standard [OpenThread CLI](https://github.com/openthread/openthread/blob/main/src/cli/README.md).

## Enabling the extension commands

To enable OpenThread extension commands, the following Kconfig option needs to be enabled:
`OpenThread Extension CLI` -> `Enable Espressif's extended features`.

## Commands

* [ip](#ip)
* [iperf](#iperf)
* [tcpsockclient](#tcpsockclient)
* [tcpsockserver](#tcpsockserver)
* [udpsockclient](#udpsockclient)
* [udpsockserver](#udpsockserver)
* [wifi](#wifi)
* [ota](#ota)
* [dns64server](#dns64server)
* [curl](#curl)

### ip

The ip command is used to add an address onto an interface or delete an address from an interface.

* General Options

```bash
> ip
----ip parameter---
print                    :     print all ip on each interface of lwip
add <ifname> <ifaddr>    :     add an address onto an interface of lwip
del <ifname> <ifaddr>    :     delete an address from an interface of lwip
Done
```

* Typical usage

Print all ip addresses on each interface of lwip:
```bash
> ip print
netif: ot
ot inet6: FE80::EC7C:3806:85C6:71C2 48
ot inet6: FDDE:AD00:BEEF:0:23AA:A8:5E2C:E03A 16
netif: lo
lo inet6: ::1 16
Done
```

Add an address onto the openthread interface of lwip:
```bash
> ip add ot fd00::2
Done
```

Delete an address from the openthread interface of lwip:
```bash
> ip del ot fd00::2
Done
```

**Note: Currently the ip commands only support adding or deleting the addresses of openthread interface and Wi-Fi interface.**

### iperf

Iperf is a tool for performing TCP or UDP throughput on the Thread network.

For running iperf, you need to have two Thread devices on the same network.

* General Options

```bash
> iperf
---iperf parameter---
-s                  :     server mode, only receive
-u                  :     upd mode
-V                  :     use IPV6 address
-c <addr>           :     client mode, only transmit
-i <interval>       :     seconds between periodic bandwidth reports
-t <time>           :     time in seconds to transmit for (default 10 secs)
-p <port>           :     server port to listen on/connect to
-l <len_send_buf>   :     the length of send buffer
-f <output_format>  :     the output format of the report (Mbit/sec, Kbit/sec, bit/sec; default Mbit/sec)
---example---
create a tcp server :     iperf -s -i 3 -p 5001 -t 60 -f M
create a udp client :     iperf -c <addr> -u -i 3 -t 60 -p 5001 -l 512 -f B
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

* General Options

```bash
> udpsockserver
---udpsockserver parameter---
status                           :     get UDP server status
open                             :     open UDP server function
bind <port>                      :     create a UDP server with binding the port
send <ipaddr> <port> <message>   :     send a message to the UDP client
close                            :     close UDP server
---example---
get UDP server status            :     udpsockserver status
open UDP server function         :     udpsockserver open
create a UDP server              :     udpsockserver bind 12345
send a message                   :     udpsockserver send FDDE:AD00:BEEF:CAFE:FD14:30B6:CDA:8A95 12346 hello
close UDP server                 :     udpsockserver close
Done
```

* Typical usage

Open the udp server function.

```bash
> udpsockserver open
Done
```

Create a udp server with binding the port.

```bash
> udpsockserver bind 12345
Done
I (411174) ot_socket: Socket created
I (411174) ot_socket: Socket bound, ipaddr ::, port 12345
I (411184) ot_socket: Successfully created
```

Check the status of udp client

```bash
> udpsockserver status
open        local ipaddr: ::        local port: 12345
Done
```

Send a message to the udp client.

```bash
> udpsockserver send fdf9:2548:ce39:efbb:9612:c4a0:477b:349a 12346 hello
Done
I (224674) ot_socket: Sending to fdf9:2548:ce39:efbb:9612:c4a0:477b:349a : 12346
```

Receive a message from the udp client.

```bash
I (278524) ot_socket: sock 54 Received 5 bytes from FDF9:2548:CE39:EFBB:9612:C4A0:477B:349A : 12346
I (278524) ot_socket: hello
```

Close the udp server.

```bash
> udpsockserver close
Done
I (308914) ot_socket: UDP server receive task exiting
I (308914) ot_socket: Closed UDP server successfully
```

### udpsockclient

Used for creating a udp client.

* General Options

```bash
> udpsockclient
---udpsockclient parameter---
status                               :     get UDP client status
open <port>                          :     open UDP client function, create a UDP client and bind a local port(optional)
send <ipaddr> <port> <message>       :     send a message to the UDP server
close                                :     close UDP client
---example---
get UDP client status                :     udpsockclient status
create a UDP client without binding  :     udpsockclient open
create a UDP client with binding     :     udpsockclient open 12345
send a message                       :     udpsockclient send FDDE:AD00:BEEF:CAFE:FD14:30B6:CDA:8A95 12346 hello
close UDP client                     :     udpsockclient close
Done
```

* Typical usage

Open the udp client function, create a udp client without binding the port.

```bash
> udpsockclient open
Done
I (842586) ot_socket: Socket created
I (842586) ot_socket: Successfully created
```

Open the udp client function, create a udp client with binding the port.

```bash
udpsockclient open 12345
Done
I (926816) ot_socket: Socket created
I (926816) ot_socket: Socket bound, port 12345
I (926816) ot_socket: Successfully created
```

Check the status of udp client

```bash
> udpsockclient status
open    local port: 12345
Done
```

Send a message to the udp client.

```bash
> udpsockclient send fdf9:2548:ce39:efbb:79b9:4ac4:f686:8fc9 12346 hello
Done
I (1180356) ot_socket: Sending to fdf9:2548:ce39:efbb:79b9:4ac4:f686:8fc9 : 12346
```

Receive a message from the udp client.

```bash
I (1218636) ot_socket: sock 54 Received 5 bytes from FDF9:2548:CE39:EFBB:79B9:4AC4:F686:8FC9 : 12345
I (1218636) ot_socket: hello
```

Close the udp client.

```bash
> udpsockclient close
Done
I (1238686) ot_socket: UDP client receive task exiting
I (1238686) ot_socket: Closed UDP client successfully
```

### wifi

Used for connecting the border router to the Wi-Fi network.

```bash
> wifi
---wifi parameter---
connect -s <ssid> -p <psk>               :      connect to a wifi network with an ssid and a psk
connect -s <ssid>                        :      connect to a wifi network with an ssid
disconnect                               :      wifi disconnect once, only for test
disconnect <delay>                       :      wifi disconnect, and reconnect after delay(ms), only for test
state                                    :      get wifi state, disconnect or connect
mac <role>                               :      get mac address of wifi netif, <role> can be "sta" or "ap"
---example---
join a wifi:
ssid: threadcertAP
psk: threadcertAP                        :      wifi connect -s threadcertAP -p threadcertAP
join a wifi:
ssid: threadAP
does not have a psk                      :      wifi connect -s threadAP
get wifi state                           :      wifi state
wifi disconnect once                     :      wifi disconnect
wifi disconnec, and reconnect after 2s   :      wifi disconnect 2000
get mac address of WiFi station          :      wifi mac sta
get mac address of WiFi soft-AP          :      wifi mac ap
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
