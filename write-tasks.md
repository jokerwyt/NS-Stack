WT-1
1. 827
2. Destination: Broadcast (ff:ff:ff:ff:ff:ff)
   It's the broadcast address.
3. 0x15

WT-2
1. Target MAC address.
2. 6 ipv4 packets.
3. 20 bytes for IPv4, 40 byte for IPv6.

WT-3
I adopt ARP protocol to get the corresponding MAC address 
by the destination IP. The ARP protocol will send a broadcast message 
which will be replied by the destination host.

WT-4
My routing subsystem is splited into two parts: static and dynamic.
The static part includes scope link (i.e. direct subnet) and manual entries.
The dynamic part bases on Distance Vector algorithm. A daemon thread is launched for 
periodly sending distance vector update to all neighbours. Once a host receives
a distance vector update, it will try to update its distance vector and the 
dynamic routing table.

WT-5
1. 2 sessions, 12 and 1652 segments, respectively.
2. 
   - 10.0.0.74, 43120, 115.27.207.221, 80
   - 10.0.0.74, 43122, 115.27.207.221, 80

3. raw Window: 85; Window size scaling factor: 512; Calculated Window size 85 * 512 = 43520. 