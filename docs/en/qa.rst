******
5. FAQ
******

Q1: Why can't I access the host from the BR device using the `ping` command?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A1: The Espressif Thread Border Router (BR) framework integrates the Openthread stack and the Wi-Fi stack as a single network interfaces (netif) connected to lwIP. Both Openthread and Wi-Fi addresses are managed by LwIP.

.. code-block::

   -------                                                  ------------
   |     |                                                  |  lwIP    |
   |  H  |                                                  |   -----  |
   |  O  |                                                  |   |   |  |
   |  S  |                                                  |-↑-↓---↑--|
   |  T  | <---BR sends the Ping Request to Host --------   | |   | |  |
   |     | ----Host sends the Ping Reply back to BR-------->|-|   | |--|<------Ping command is 
   |     |                                                  | WiFi| OT |       implemented at the OT layer
   |     |                                                  |     |    |
   -------                                                  ------------


LwIP serves as the upper layer for Openthread and Wi-Fi, forwarding packets from each netif. The ping command you used operates at the Openthread stack layer. In our BR setup, ping packets must be sent to the LWIP layer, which then forwards them to the Wi-Fi interface.

Here's how it works in your scenario:

1. The ping command initiates at the Openthread layer. The ping packet is sent to lwIP for forwarding.
2. LwIP recognizes the destination address on the Wi-Fi netif then forwards the packets to the Wi-Fi interface.
3. The host receives the packets and sends a reply to the BR.
4. The BR receives the reply packets on the Wi-Fi interface.The BR pushes the reply packets to lwIP for processing.
5. LwIP finds the destination address is assigned to the BR (Openthread layer assigns the OMR address to lwIP). LwIP does not need to forward the packets back to the Openthread layer.

As a result, the Openthread layer shows the ping reply as lost, even though the BR receives the reply packets.

To resolve this (i.e., to see a successful ping result from BR to Host), you can implement a ping command at the lwIP layer. Using this command, you can ping the Host directly from lwIP.