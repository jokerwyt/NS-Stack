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

// return nullptr or 0 if id is invalid.
const struct ether_addr* dev_mac(int id);
const struct in_addr* dev_ip(int id);
const struct in_addr* dev_mask(int id);

// return 1 for valid and 0 for invalid.
int is_valid_id(int id);

// return NULL for invalid.
pcap_t* get_pcap_handle(int id);

// list possible devices
// n is the number of devices.
// dont forget to free the list.
char ** get_host_device_lists(int *n);

const char * get_device_name(int id);

// return -1 for not neighbour, dev_id for the first match device.
// we suppose there is only one match.
int get_dev_from_subnet(const struct in_addr ip, const struct in_addr mask);

int get_dev_cnt();