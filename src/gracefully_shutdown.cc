#include "gracefully_shutdown.h"


#include <mutex>
#include <vector>

std::vector<std::function<void()>> clean_up_funcs;

void add_exit_clean_up(std::function<void()> func) {
    static std::mutex mutex;
    static bool init = false;
    std::lock_guard<std::mutex> lock(mutex);

    if (init == false) {
        init = true;
        std::atexit([]() {
            for (auto& func : clean_up_funcs) {
                func();
            }
        });
    }

    clean_up_funcs.push_back(func);
}