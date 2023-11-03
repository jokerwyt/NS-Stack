#pragma once

#include <functional>

#define EXIT_CLEAN_UP_PRIORITY_SOCKET 0
#define EXIT_CLEAN_UP_PRIORITY_TCP 100
#define EXIT_CLEAN_UP_PRIORITY_IP 200
#define EXIT_CLEAN_UP_PRIORITY_LINK 300


void add_exit_clean_up(std::function<void()> func, int priority);