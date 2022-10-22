#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <cassert>
#include <vector>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <deque>

#include "src/hash_set_base.h"

template <typename T>
class HashSetStriped : public HashSetBase<T> {
public:
    explicit HashSetStriped(size_t capacity) : bucket_count_(capacity), initial_bucket_count_(capacity), elem_count_(0) {
        table_.resize(bucket_count_);
        mutexes_ = std::vector<std::mutex>(initial_bucket_count_);
        for (size_t i = 0; i < bucket_count_; i++) {
            table_[i] = std::vector<T>();
        }

    }

    bool Add(T elem) final {
        size_t my_lock = std::hash<T>()(elem) % initial_bucket_count_;
        mutexes_[my_lock].lock();
        size_t my_bucket = std::hash<T>()(elem) % bucket_count_;
        if (ContainsNoLock(elem, my_bucket)) {
            mutexes_[my_lock].unlock();
            return false;
        }
        table_[my_bucket].push_back(elem);
        elem_count_.fetch_add(1);
        mutexes_[my_lock].unlock();
        if (Policy()) {
            for (size_t i = 0; i < mutexes_.size(); i ++) {
                mutexes_[i].lock();
            }
            Resize();
            for(size_t i = 0; i < mutexes_.size(); i ++) {
                mutexes_[i].unlock();
            }
        }
        return true;
    }

    bool Remove(T elem) final {
        size_t my_lock = std::hash<T>()(elem) % initial_bucket_count_;
        std::scoped_lock<std::mutex> lock(mutexes_[my_lock]);
        size_t my_bucket = std::hash<T>()(elem) % bucket_count_;
        if (!ContainsNoLock(elem, my_bucket)) return false;
        assert(elem_count_ != 0);
        table_[my_bucket].erase(std::find(table_[my_bucket].begin(), table_[my_bucket].end(), elem));
        elem_count_.fetch_sub(1);
        return true;
    }

    [[nodiscard]] bool Contains(T elem) final {
        size_t my_lock = std::hash<T>()(elem) % initial_bucket_count_;
        std::scoped_lock<std::mutex> lock(mutexes_[my_lock]);
        size_t my_bucket = std::hash<T>()(elem) % bucket_count_;
        return ContainsNoLock(elem, my_bucket);
    }

    [[nodiscard]] size_t Size() const final {
        return elem_count_.load();
    }

private:

    std::vector<std::vector<T>> table_;
    size_t bucket_count_;
    size_t initial_bucket_count_;
    std::atomic<size_t> elem_count_;
    std::vector<std::mutex> mutexes_;
    std::mutex resize_mutex_;

    bool ContainsNoLock(T elem, size_t my_bucket) {
        std::__1::vector<T> small_table = table_[my_bucket];
        return std::find(small_table.begin(), small_table.end(), elem) != small_table.end();
    }

    bool Policy() {
        return (elem_count_.load() / bucket_count_) > 4;
    }

    void Resize() {
        size_t old_capacity = bucket_count_;
        size_t new_capacity = 2 * old_capacity;
        bucket_count_ = new_capacity;
        std::vector<std::vector<T>> table;
        table.resize(new_capacity);
        for (size_t i = 0; i < new_capacity; i++) {
            table[i] = std::vector<T>();
        }
        for (std::vector<T> bucket : table_) {
            for (T elem : bucket) {
                table[std::hash<T>()(elem) % new_capacity].push_back(elem);
            }
        }
        table_ = table;
    }
};

#endif  // HASH_SET_STRIPED_H
