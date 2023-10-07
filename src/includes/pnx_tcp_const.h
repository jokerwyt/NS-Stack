#pragma once

const size_t kTcpSendBufferSize = 4096;
const size_t kTcpRecvBufferSize = 4096;
const size_t kTcpMaxSegmentSize = 1024;
const size_t kTcpMSL = 1000000; // us
const size_t kTcpTimeout = 1000000; // us = 1000ms
const size_t kTcpMaxRetrans = 100; // last for one second