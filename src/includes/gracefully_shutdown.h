#pragma once

#include <functional>

void add_exit_clean_up(std::function<void()> func);