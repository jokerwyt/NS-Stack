#!/usr/bin/env bash
ROOT_DIR=$(dirname $(dirname $(realpath $0)))
TBIN_DIR=$ROOT_DIR/build/tests


cleanup() {
    echo "Recv SIGINT. clean up..."
    sleep 1
    pkill lab1
    exit 0
}
trap cleanup SIGINT

# This script is used to show the result of lab1
# make the example vnet namespace first.
pkill lab1

export PNX_NOCOLOR=1
# server
$ROOT_DIR/vnetUtils/helper/execNS ns1 $TBIN_DIR/lab1 &

sleep 1

# client. MAC addr from ns1 device
$ROOT_DIR/vnetUtils/helper/execNS ns2 $TBIN_DIR/lab1 5a:f1:43:d9:1e:eb &

wait
