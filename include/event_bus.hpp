#include <any>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <thread>
#include <stop_token>

class event_bus {
public:
    template<typename Event>
    void on(std::function<void(const Event&)> handler) {
        std::scoped_lock lock(mutex_);
        auto& vec = handlers_[typeid(Event)];
        vec.push_back([h = std::move(handler)](const std::any& e) {
            h(std::any_cast<const Event&>(e));
            });
    }

    template<typename Event>
    void fire(const Event& event) {
        std::scoped_lock lock(mutex_);
        auto it = handlers_.find(typeid(Event));
        if (it != handlers_.end()) {
            for (auto& handler : it->second) {
                handler(event);
            }
        }
    }

    template<typename Event>
    void fireAsync(const Event& event) {
        std::vector<std::function<void(std::any)>> vecCopy;
        {
            std::scoped_lock lock(mutex_);
            auto it = handlers_.find(typeid(Event));
            if (it != handlers_.end())
                vecCopy = it->second;
        }
        for (auto& handler : vecCopy) {
            std::jthread([handler, event](std::stop_token) { handler(event); });
        }
    }

private:
    std::unordered_map<std::type_index, std::vector<std::function<void(std::any)>>> handlers_;
    std::mutex mutex_;
};
