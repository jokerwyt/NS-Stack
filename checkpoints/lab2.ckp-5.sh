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
    $UTILS/execNS ns5 $UTILS/undoBypass
    $UTILS/execNS ns6 $UTILS/undoBypass


    exit 0
}
trap cleanup SIGINT


rm /tmp/ns*
$UTILS/execNS ns1 $UTILS/bypassKernel
$UTILS/execNS ns2 $UTILS/bypassKernel
$UTILS/execNS ns3 $UTILS/bypassKernel
$UTILS/execNS ns4 $UTILS/bypassKernel
$UTILS/execNS ns5 $UTILS/bypassKernel
$UTILS/execNS ns6 $UTILS/bypassKernel

pkill lab2

PNX_LOG_PREFIX=ns1 $ROOT_DIR/vnetUtils/helper/execNS ns1 $TBIN_DIR/lab2 -dveth1-2 > /tmp/ns1 &
PNX_LOG_PREFIX=ns2 $ROOT_DIR/vnetUtils/helper/execNS ns2 $TBIN_DIR/lab2 -dveth2-1,veth2-3,veth2-5 > /tmp/ns2 &
PNX_LOG_PREFIX=ns3 $ROOT_DIR/vnetUtils/helper/execNS ns3 $TBIN_DIR/lab2 -dveth3-2,veth3-4,veth3-6 > /tmp/ns3 &
PNX_LOG_PREFIX=ns4 $ROOT_DIR/vnetUtils/helper/execNS ns4 $TBIN_DIR/lab2 -dveth4-3  > /tmp/ns4 &
PNX_LOG_PREFIX=ns5 $ROOT_DIR/vnetUtils/helper/execNS ns5 $TBIN_DIR/lab2 -dveth5-2,veth5-6  > /tmp/ns5 &
PNX_LOG_PREFIX=ns6 $ROOT_DIR/vnetUtils/helper/execNS ns6 $TBIN_DIR/lab2 -dveth6-5,veth6-3  > /tmp/ns6 &

wait
