#pragma once

const size_t kTcpSendBufferSize = 16384;
// const size_t kTcpRecvBufferSize = 16384;
const size_t kTcpMaxSegmentSize = 1024;
const size_t kTcpMSL = 1000000; // us
const size_t kTcpTimeout = 1e5; // us 
const int kTcpMaxRetrans = 100; // last for one second