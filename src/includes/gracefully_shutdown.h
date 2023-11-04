#pragma once

#include <functional>

#define EXIT_CLEAN_UP_PRIORITY_LINK_RECVING 0
#define EXIT_CLEAN_UP_PRIORITY_IP_RECVING 100
#define EXIT_CLEAN_UP_PRIORITY_TCP_RECVING 200
#define EXIT_CLEAN_UP_PRIORITY_SOCKET_RECVING 300

#define EXIT_CLEAN_UP_PRIORITY_IP_SENDING 400


void add_exit_clean_up(std::function<void()> func, int priority);