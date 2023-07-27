#pragma once
/**
* @file device.h
* @brief Library supporting network device management. 
*/

#include <pcap.h>

/**
* Add a device to the library for sending/receiving packets. 
*
* @param device Name of network device to send/receive packet on.
* @return A non-negative _device-ID_ on success, -1 on error. 
*/
int add_device(const char* device);

/**
* Find a device added by ‘addDevice‘. 
*
* @param device Name of the network device.
* @return A non-negative _device-ID_ on success, -1 if no such device
* was found. 
*/
int find_device(const char* device);

// return NULL if id is invalid.
char* dev_mac(int id);

// return 1 for valid and 0 for invalid.
int is_valid_id(int id);

// return NULL for invalid.
pcap_t* get_pcap_handle(int id);

// list possible devices
// TODO: it's not an API for use now.  let it return char**.
void print_device_lists();
