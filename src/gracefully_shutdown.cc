#include "gracefully_shutdown.h"


#include <mutex>
#include <vector>

#include "logger.h"



void add_exit_clean_up(std::function<void()> func, int priority) {
    static std::mutex mutex;
    static bool init = false;
    static std::vector<std::pair<int, std::function<void()>>> clean_up_funcs;
    std::lock_guard<std::mutex> lock(mutex);

    if (init == false) {
        init = true;
        std::atexit([]() {
            std::sort(clean_up_funcs.begin(), clean_up_funcs.end(), 
                [](const auto& a, const auto& b) {
                    return a.first < b.first;
                }); // sort by priority
            for (auto& func : clean_up_funcs) {
                logInfo("clean up function with priority %d", func.first);
                func.second();
            }
        });
    }

    clean_up_funcs.push_back(std::make_pair(priority, func));
}