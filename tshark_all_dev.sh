#!/usr/bin/env bash
# tshark all device of the example topology.

ROOT_DIR=$(dirname $(realpath $0))
TBIN_DIR=$ROOT_DIR/build/tests
UTILS=$ROOT_DIR/vnetUtils/helper

cleanup() {
    pkill tshark
    exit 0
}
trap cleanup SIGINT




rm /dmount/veth*.pcap

$UTILS/execNS ns1 tshark -iveth1-2 -w/dmount/veth1-2.pcap &
$UTILS/execNS ns2 tshark -iveth2-1 -w/dmount/veth2-1.pcap &
$UTILS/execNS ns2 tshark -iveth2-3 -w/dmount/veth2-3.pcap &
$UTILS/execNS ns3 tshark -iveth3-2 -w/dmount/veth3-2.pcap &
$UTILS/execNS ns3 tshark -iveth3-4 -w/dmount/veth3-4.pcap &
$UTILS/execNS ns4 tshark -iveth4-3 -w/dmount/veth4-3.pcap &


wait