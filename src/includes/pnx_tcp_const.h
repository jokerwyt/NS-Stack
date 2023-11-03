#pragma once

#include <memory>

const size_t kTcpSendBufferSize = (1 << 20);
const size_t kTcpRecvBufferSize = (1 << 20);
const size_t kTcpMaxSegmentSize = 1024;
const size_t kTcpTimeout = 1e3; // us
const size_t kTcpMSL = 1000000; // us
const int kTcpMaxRetrans = 100; // last for one second