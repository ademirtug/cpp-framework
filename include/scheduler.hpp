#include "datetime.hpp"
#include <functional>
#include <stop_token>
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>

namespace framework {

    class Scheduler {
    public:
        // Run task every interval
        void runEvery(std::chrono::milliseconds interval, std::function<void()> task) {
            std::jthread t([interval, task](std::stop_token st) {
                while (!st.stop_requested()) {
                    auto next = std::chrono::steady_clock::now() + interval;
                    task();
                    std::this_thread::sleep_until(next);
                }
                });
            threads_.push_back(std::move(t));
        }

        // Run task at a specific datetime every day (same hour/minute each day)
        void runDailyAt(const datetime& dt, std::function<void()> task) {
            std::jthread t([dt, task](std::stop_token st) {
                using namespace std::chrono;
                while (!st.stop_requested()) {
                    auto now = system_clock::now();
                    std::time_t tnow = system_clock::to_time_t(now);
                    std::tm tm_now;
#ifdef _WIN32
                    localtime_s(&tm_now, &tnow);
#else
                    localtime_r(&tnow, &tm_now);
#endif
                    tm_now.tm_hour = dt.hour();
                    tm_now.tm_min = dt.minute();
                    tm_now.tm_sec = dt.second();

                    auto next = system_clock::from_time_t(std::mktime(&tm_now));
                    if (next <= now) next += hours(24);

                    std::this_thread::sleep_until(next);
                    if (st.stop_requested()) break;

                    task();
                }
                });
            threads_.push_back(std::move(t));
        }

        // Run task at a specific datetime once
        void runAt(const datetime& dt, std::function<void()> task) {
            std::jthread t([dt, task](std::stop_token st) {
                auto next = dt.time_point();
                std::this_thread::sleep_until(next);
                if (!st.stop_requested()) {
                    task();
                }
                });
            threads_.push_back(std::move(t));
        }

        void stopAll() {
            for (auto& t : threads_) t.request_stop();
            threads_.clear();
        }

    private:
        std::vector<std::jthread> threads_;
    };

} // namespace framework
