#!/usr/bin/env bash
ROOT_DIR=$(dirname $(dirname $(realpath $0)))
TBIN_DIR=$ROOT_DIR/build/tests
UTILS=$ROOT_DIR/vnetUtils/helper/

cleanup() {
    echo "Recv SIGINT. clean up..."
    sleep 1
    pkill lab2
    $UTILS/execNS ns1 $UTILS/undoBypass
    $UTILS/execNS ns2 $UTILS/undoBypass
    $UTILS/execNS ns3 $UTILS/undoBypass
    $UTILS/execNS ns4 $UTILS/undoBypass


    exit 0
}
trap cleanup SIGINT


rm /tmp/ns*
$UTILS/execNS ns1 $UTILS/bypassKernel
$UTILS/execNS ns2 $UTILS/bypassKernel
$UTILS/execNS ns3 $UTILS/bypassKernel
$UTILS/execNS ns4 $UTILS/bypassKernel



pkill lab2

# export PNX_NOCOLOR=1
# server in ns1
PNX_LOG_PREFIX=ns1-server $ROOT_DIR/vnetUtils/helper/execNS ns1 $TBIN_DIR/lab2 -dveth1-2 &

# ns2
PNX_LOG_PREFIX=ns2 $ROOT_DIR/vnetUtils/helper/execNS ns2 $TBIN_DIR/lab2 -dveth2-1,veth2-3 > /tmp/ns2&

# ns3
PNX_LOG_PREFIX=ns3 $ROOT_DIR/vnetUtils/helper/execNS ns3 $TBIN_DIR/lab2 -dveth3-2,veth3-4 > /tmp/ns3 &

# ns4
PNX_LOG_PREFIX=ns4-client $ROOT_DIR/vnetUtils/helper/execNS ns4 $TBIN_DIR/lab2 -dveth4-3 10.100.1.1 &

wait
