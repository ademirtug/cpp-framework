// ecs_s.hpp
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace framework {

    using entity = std::uint64_t;
    using component_id = std::uint64_t;

    // Forward declaration
    class registry;

    // Base for type erasure: enough to remove and query
    struct base_component_storage {
        virtual ~base_component_storage() = default;
        virtual void erase(entity e) = 0;
        virtual bool has(entity e) const = 0;
    };

    // Sparse set with dynamic capacity
    template <typename T>
    class sparse_set {
        struct storage {
            entity index{ static_cast<entity>(-1) };
            T      payload;
        };

        std::vector<std::size_t> sparse_;  // maps entity -> dense index (or -1)
        std::vector<storage>      dense_;   // packed payloads + entity indices
        std::size_t               n{ 0 };   // logical size (# of valid entries in [0, n))
        static inline const std::size_t max_capacity = 1'000'000; // configurable

        void grow_sparse_to(entity index) {
            if (index >= sparse_.size()) {
                const std::size_t want = static_cast<std::size_t>(index) + 1;
                const std::size_t new_size =
                    std::max(sparse_.size() * 2, want);
                if (new_size > max_capacity) {
                    throw std::out_of_range("Entity index exceeds maximum sparse capacity");
                }
                sparse_.resize(new_size, static_cast<std::size_t>(-1));
            }
        }

    public:
        sparse_set() : sparse_(1024, static_cast<std::size_t>(-1)) {}

        std::size_t size() const { return n; }

        bool has(entity index) const {
            return index < sparse_.size()
                && sparse_[index] != static_cast<std::size_t>(-1)
                && sparse_[index] < n
                && dense_[sparse_[index]].index == index;
        }

        T& operator[](entity index) {
            // Precondition: has(index) must be true for defined behavior.
            return dense_[sparse_[index]].payload;
        }

        const T& operator[](entity index) const {
            // Precondition: has(index) must be true for defined behavior.
            return dense_[sparse_[index]].payload;
        }

        template <typename... Args>
        void emplace(entity index, Args&&... args) {
            grow_sparse_to(index);
            if (has(index)) {
                dense_[sparse_[index]].payload = T(std::forward<Args>(args)...);
                return;
            }
            if (dense_.size() == n) dense_.emplace_back();
            else                    dense_[n] = storage{};
            dense_[n].index = index;
            dense_[n].payload = T(std::forward<Args>(args)...);
            sparse_[index] = n++;
        }

        void insert(entity index, T value) {
            emplace(index, std::move(value));
        }

        void erase(entity index) {
            if (!has(index)) return;
            std::size_t old_idx = sparse_[index];
            --n;
            if (old_idx != n) {
                dense_[old_idx] = std::move(dense_[n]);
                sparse_[dense_[old_idx].index] = old_idx;
            }
            sparse_[index] = static_cast<std::size_t>(-1);
        }

        auto begin() { return dense_.begin(); }
        auto end() { return dense_.begin() + static_cast<std::ptrdiff_t>(n); }
        auto begin() const { return dense_.begin(); }
        auto end()   const { return dense_.begin() + static_cast<std::ptrdiff_t>(n); }

        auto range() { return std::ranges::subrange(begin(), end()); }
        auto range() const { return std::ranges::subrange(begin(), end()); }
    };

    // Concept: any non-cv-qualified object type
    template <typename T>
    concept component_type = std::is_object_v<std::remove_cvref_t<T>>;

    class registry {
        inline static entity next_entity_ = 0;
        std::vector<entity>  free_entities_;

        // Independent, dense component-type IDs starting at 1.
        inline static std::atomic<component_id> next_component_id_{ 1 };

        // Type-erased storages owned by the registry
        std::unordered_map<component_id, std::unique_ptr<base_component_storage>> component_storages_;

        template <component_type T>
        struct component_storage : base_component_storage {
            sparse_set<T> data;

            void erase(entity e) override { data.erase(e); }
            bool has(entity e) const override { return data.has(e); }
        };

        template <component_type T>
        sparse_set<T>& get_storage() {
            const auto cid = get_component_id<T>();
            auto& ptr = component_storages_[cid];
            if (!ptr) {
                ptr = std::make_unique<component_storage<T>>();
            }
            return static_cast<component_storage<T>*>(ptr.get())->data;
        }

        template <component_type T>
        const sparse_set<T>& get_storage() const {
            const auto cid = get_component_id<T>();
            auto it = component_storages_.find(cid);
            if (it == component_storages_.end()) {
                static const sparse_set<T> empty; // safe, read-only empty
                return empty;
            }
            return static_cast<const component_storage<T>*>(it->second.get())->data;
        }

    public:
        // Entity lifecycle
        entity new_entity() {
            if (!free_entities_.empty()) {
                entity e = free_entities_.back();
                free_entities_.pop_back();
                return e;
            }
            return ++next_entity_;
        }

        void remove_entity(entity e) {
            for (auto& [_, storage] : component_storages_) {
                storage->erase(e);
            }
            free_entities_.push_back(e);
        }

        // Component access
        template <component_type T>
        bool has_component(entity e) const {
            const auto it = component_storages_.find(get_component_id<T>());
            if (it == component_storages_.end()) return false;
            return it->second->has(e);
        }

        template <component_type T>
        T& get_component(entity e) {
            // Precondition: has_component<T>(e) is true.
            return get_storage<T>()[e];
        }

        template <component_type T>
        const T& get_component(entity e) const {
            // Precondition: has_component<T>(e) is true.
            return get_storage<T>()[e];
        }

        template <component_type T>
        T* try_get_component(entity e) {
            auto it = component_storages_.find(get_component_id<T>());
            if (it == component_storages_.end()) return nullptr;
            auto& storage = static_cast<component_storage<T>*>(it->second.get())->data;
            return storage.has(e) ? &storage[e] : nullptr;
        }

        template <component_type T>
        const T* try_get_component(entity e) const {
            auto it = component_storages_.find(get_component_id<T>());
            if (it == component_storages_.end()) return nullptr;
            auto const& storage = static_cast<const component_storage<T>*>(it->second.get())->data;
            return storage.has(e) ? &storage[e] : nullptr;
        }

        // Add/remove
        template <component_type T, typename... Args>
        void add_component(entity e, Args&&... args) {
            get_storage<T>().emplace(e, std::forward<Args>(args)...);
        }

        template <component_type T>
        void remove_component(entity e) {
            get_storage<T>().erase(e);
        }

        // Check if entity has all components
        template <component_type... Ts>
        bool has_all(entity e) const {
            return (has_component<Ts>(e) && ...);
        }

        // Generate unique ID per type (1..N)
        template <component_type T>
        [[nodiscard]] static component_id get_component_id() {
            static component_id id =
                next_component_id_.fetch_add(1, std::memory_order_relaxed);
            return id;
        }

        // Iterate all entities with component T
        template <component_type T, std::invocable<entity, T&> F>
        void each(F&& f) {
            auto& storage = get_storage<T>();
            for (auto& item : storage.range()) {
                f(item.index, item.payload);
            }
        }

        // Iterate all entities with all components Ts...
        template <component_type... Ts, std::invocable<entity, Ts&...> F>
        void view(F&& f) {
            static_assert(sizeof...(Ts) >= 1);
            using first_t = std::tuple_element_t<0, std::tuple<Ts...>>;

            auto* smallest = &get_storage<first_t>();
            (((get_storage<Ts>().size() < smallest->size()
                ? smallest = &get_storage<Ts>()
                : smallest), void()), ...);

            for (auto& item : smallest->range()) {
                const entity e = item.index;
                if (has_all<Ts...>(e)) {
                    std::invoke(std::forward<F>(f), e, get_component<Ts>(e)...);
                }
            }
        }
    };

} // namespace ecs_s
