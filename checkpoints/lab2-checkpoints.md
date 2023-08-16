# Checkpoint 3
```
0000   2e 5f bd 9e 9f d4 06 f8 09 1b a4 c7 08 00 45 00
0010   00 90 00 00 00 00 40 06 61 9e 0a 64 03 02 0a 64
0020   01 01 33 31 34 30 33 31 39 37 37 35 30 32 20 68
0030   65 6c 6c 6f 20 77 6f 72 6c 64 21 20 69 61 6d 70
0040   61 64 64 69 6e 67 20 69 61 6d 70 61 64 64 69 6e
0050   67 20 69 61 6d 70 61 64 64 69 6e 67 20 69 61 6d
0060   70 61 64 64 69 6e 67 20 69 61 6d 70 61 64 64 69
0070   6e 67 20 69 61 6d 70 61 64 64 69 6e 67 20 69 61
0080   6d 70 61 64 64 69 6e 67 20 69 61 6d 70 61 64 64
0090   69 6e 67 20 69 61 6d 70 61 64 64 69 6e 67 00 00
00a0   00 00
```

The following 14 bytes are the Ethernet header.  
* The first six bytes are the destination MAC address. 
* The next six bytes are the source MAC address. 

The following 20 bytes are the IP header. 
* The first four bits `0100` means the protocol version IPv4. 
* The next four bits `0101` means the IP header length is 5*4=20 bytes. 
* The next 8 bits are Services Field. The first six bits are Differentialted Services 
Codepoint and the next two bits are ECN bits for congestion control.
* The next two bytes are the total length 0x0090 = 144. 
* The next two bytes are the default identification 0.  
* The next three bits are IP flags, Reserved bit, Don't fragment bit and More fragments 
respectively. Since I have not implemented IP Fragmentation yet, they are all default 0. 
* The next 5+8=13 bits is the fragment offset 0. The IP flags and fragment offset 
mean this IP packet is a single fragmentable and unfragmented packet. 
* The next byte are Time to Live 64, which means this packet should live at most 64 seconds or 64 hops.
* The next byte are the upper protocol TCP code 0x06.
* The next two bytes are IP header checksum.
* The next four bytes are IP source address 10.100.3.2
* The next four bytes are IP destination address 10.100.1.1

* And we have a 144-20=124 IP payload.

At last we have a four bytes FCS checksum for Ethernet.



# Checkpoint 4

First we construct a example virtual network topology with `example.txt`: `ns1-ns2-ns3-ns4`, and disable all kernel IP forwarding and ARP handler.

Then we use the script `checkpoints/lab2-ckp-3-4.sh` to launch four servers, one for each virtual namespace. 

Once the servers are launched, they probe the neighbour subnets and exchange distance vector to establish routing tables. This procedure needs several seconds.  

## (1) ns1 can discover ns4

The ns4 server will keep sending  IP packets to the ns1 server. After the routing tables are established, the IP packet will reach ns1. 

![image-20230816144335678](/Users/yongtongwu/dmount/lab-netstack-premium/checkpoints/lab2-checkpoints.assets/image-20230816144335678.png)



## (2) and (3) 

We first disconnect ns2, and launch ns2 server later. You can see log in ns1:

![image-20230816145521159](/Users/yongtongwu/dmount/lab-netstack-premium/checkpoints/lab2-checkpoints.assets/image-20230816145521159.png)



# Checkpoint 5



Summary

```
Full topology
Hop 1 2 3 4 5 6
1		\	0	1	2 1 2
2		0 \	0 1 0 1
3		1 0 \ 0 1 0
4		2 1 0 \	2 1
5		1	0	1	2	\ 0
6		2	1	0	1	0	\

Disconnecting ns5 does not change any distance.
```



Detail log

```

ns1
routing.cc:409 distance vector:
routing.cc:411 dest=10.100.3.0, mask=255.255.255.0, hops=2, updated_from=10.100.1.2
routing.cc:411 dest=10.100.6.0, mask=255.255.255.0, hops=2, updated_from=10.100.1.2
routing.cc:411 dest=10.100.5.0, mask=255.255.255.0, hops=2, updated_from=10.100.1.2
routing.cc:411 dest=10.100.4.0, mask=255.255.255.0, hops=1, updated_from=10.100.1.2
routing.cc:411 dest=10.100.2.0, mask=255.255.255.0, hops=1, updated_from=10.100.1.2
routing.cc:425 dynamic routing table:
routing.cc:427 dest=10.100.3.0, mask=255.255.255.0, next_hop=10.100.1.2, device=veth1-2
routing.cc:427 dest=10.100.6.0, mask=255.255.255.0, next_hop=10.100.1.2, device=veth1-2
routing.cc:427 dest=10.100.5.0, mask=255.255.255.0, next_hop=10.100.1.2, device=veth1-2
routing.cc:427 dest=10.100.4.0, mask=255.255.255.0, next_hop=10.100.1.2, device=veth1-2
routing.cc:427 dest=10.100.2.0, mask=255.255.255.0, next_hop=10.100.1.2, device=veth1-2
routing.cc:450 routing table updated.


ns2
routing.cc:409 distance vector:
routing.cc:411 dest=10.100.5.0, mask=255.255.255.0, hops=1, updated_from=10.100.4.2
routing.cc:411 dest=10.100.6.0, mask=255.255.255.0, hops=1, updated_from=10.100.2.2
routing.cc:411 dest=10.100.3.0, mask=255.255.255.0, hops=1, updated_from=10.100.2.2
routing.cc:425 dynamic routing table:
routing.cc:427 dest=10.100.5.0, mask=255.255.255.0, next_hop=10.100.4.2, device=veth2-5
routing.cc:427 dest=10.100.6.0, mask=255.255.255.0, next_hop=10.100.2.2, device=veth2-3
routing.cc:427 dest=10.100.3.0, mask=255.255.255.0, next_hop=10.100.2.2, device=veth2-3
outing.cc:450 routing table updated.

ns3
routing.cc:409 distance vector:
routing.cc:411 dest=10.100.5.0, mask=255.255.255.0, hops=1, updated_from=10.100.6.2
routing.cc:411 dest=10.100.4.0, mask=255.255.255.0, hops=1, updated_from=10.100.2.1
routing.cc:411 dest=10.100.1.0, mask=255.255.255.0, hops=1, updated_from=10.100.2.1
routing.cc:425 dynamic routing table:
routing.cc:427 dest=10.100.5.0, mask=255.255.255.0, next_hop=10.100.6.2, device=veth3-6
routing.cc:427 dest=10.100.4.0, mask=255.255.255.0, next_hop=10.100.2.1, device=veth3-2
routing.cc:427 dest=10.100.1.0, mask=255.255.255.0, next_hop=10.100.2.1, device=veth3-2
routing.cc:450 routing table updated.

ns4 
routing.cc:409 distance vector:
routing.cc:411 dest=10.100.1.0, mask=255.255.255.0, hops=2, updated_from=10.100.3.1
routing.cc:411 dest=10.100.4.0, mask=255.255.255.0, hops=2, updated_from=10.100.3.1
routing.cc:411 dest=10.100.5.0, mask=255.255.255.0, hops=2, updated_from=10.100.3.1
routing.cc:411 dest=10.100.6.0, mask=255.255.255.0, hops=1, updated_from=10.100.3.1
routing.cc:411 dest=10.100.2.0, mask=255.255.255.0, hops=1, updated_from=10.100.3.1
routing.cc:425 dynamic routing table:
routing.cc:427 dest=10.100.1.0, mask=255.255.255.0, next_hop=10.100.3.1, device=veth4-3
routing.cc:427 dest=10.100.4.0, mask=255.255.255.0, next_hop=10.100.3.1, device=veth4-3
routing.cc:427 dest=10.100.5.0, mask=255.255.255.0, next_hop=10.100.3.1, device=veth4-3
routing.cc:427 dest=10.100.6.0, mask=255.255.255.0, next_hop=10.100.3.1, device=veth4-3
routing.cc:427 dest=10.100.2.0, mask=255.255.255.0, next_hop=10.100.3.1, device=veth4-3
routing.cc:450 routing table updated.

ns5
routing.cc:409 distance vector:
routing.cc:411 dest=10.100.6.0, mask=255.255.255.0, hops=1, updated_from=10.100.5.2
routing.cc:411 dest=10.100.3.0, mask=255.255.255.0, hops=2, updated_from=10.100.5.2
routing.cc:411 dest=10.100.2.0, mask=255.255.255.0, hops=1, updated_from=10.100.4.1
routing.cc:411 dest=10.100.1.0, mask=255.255.255.0, hops=1, updated_from=10.100.4.1
routing.cc:425 dynamic routing table:
routing.cc:427 dest=10.100.6.0, mask=255.255.255.0, next_hop=10.100.5.2, device=veth5-6
routing.cc:427 dest=10.100.3.0, mask=255.255.255.0, next_hop=10.100.5.2, device=veth5-6
routing.cc:427 dest=10.100.2.0, mask=255.255.255.0, next_hop=10.100.4.1, device=veth5-2
routing.cc:427 dest=10.100.1.0, mask=255.255.255.0, next_hop=10.100.4.1, device=veth5-2
routing.cc:450 routing table updated.

ns6
routing.cc:409 distance vector:
routing.cc:411 dest=10.100.4.0, mask=255.255.255.0, hops=1, updated_from=10.100.5.1
routing.cc:411 dest=10.100.1.0, mask=255.255.255.0, hops=2, updated_from=10.100.5.1
routing.cc:411 dest=10.100.3.0, mask=255.255.255.0, hops=1, updated_from=10.100.6.1
routing.cc:411 dest=10.100.2.0, mask=255.255.255.0, hops=1, updated_from=10.100.6.1
routing.cc:425 dynamic routing table:
routing.cc:427 dest=10.100.4.0, mask=255.255.255.0, next_hop=10.100.5.1, device=veth6-5
routing.cc:427 dest=10.100.1.0, mask=255.255.255.0, next_hop=10.100.5.1, device=veth6-5
routing.cc:427 dest=10.100.3.0, mask=255.255.255.0, next_hop=10.100.6.1, device=veth6-3
routing.cc:427 dest=10.100.2.0, mask=255.255.255.0, next_hop=10.100.6.1, device=veth6-3
routing.cc:450 routing table updated.
```



# Checkpoint 6

All routing entry will be sorted by their subnet mask. Bigger mask means more specific rules. We query the routing table in this order.

![image-20230816162302545](/Users/yongtongwu/dmount/lab-netstack-premium/checkpoints/lab2-checkpoints.assets/image-20230816162302545.png)

![image-20230816162456498](/Users/yongtongwu/dmount/lab-netstack-premium/checkpoints/lab2-checkpoints.assets/image-20230816162456498.png)