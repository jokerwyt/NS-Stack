#pragma once
/**
* @file packetio.h
* @brief Library supporting sending/receiving Ethernet II frames. */

#include <netinet/ether.h>
#include <functional>

/**
* @brief Encapsulate some data into an Ethernet II frame and send it. 
* @param buf Pointer to the payload.
* @param len Length of the payload.
* @param ethtype EtherType field value of this frame.
* @param destmac MAC address of the destination.
* @param id ID of the device(returned by ‘addDevice‘) to send on.
* @return 0 on success, -1 on error.
* @see addDevice 
*/
int send_frame(const void* buf, int len, int ethtype, const void* destmac, int id);

/**
* @brief Process a frame upon receiving it. 
* `buf` points to the workload, instead of frame header.
* @param buf Pointer to the frame.
* @param len Length of the frame.
* @param id ID of the device (returned by ‘addDevice‘) receiving current frame.
* @return 0 on success, -1 on error.
* @see addDevice */
typedef int (*FrameReceiveCallback)(const void*, int, int);

/**
* @brief Register a callback function to be called each time 
* an Ethernet II frame was received. 
* loop threads are fired when calling add_device().
* callback should be reenterable.
* the buffer passed to callback is just ** reference **, 
* dont save the ptr or delete it.
* if you need the content, create a new copy.
*
* @param callback the callback function.
* @return always 0.
* @see frameReceiveCallback 
*/
int set_frame_receive_callback(FrameReceiveCallback callback);

// create a new thread for this device's packet receiving.
// must success, or fatal termination. always return 0.
// 
// call path:
// recv_thread_go -> frame_handler_thread -> pcap_loop 
// -> callback_wrapper -> user-given frame callback
int recv_thread_go(int device_id);