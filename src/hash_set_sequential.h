#ifndef HASH_SET_SEQUENTIAL_H
#define HASH_SET_SEQUENTIAL_H

#include <cassert>
#include <vector>
#include <algorithm>

#include "src/hash_set_base.h"

template<typename T>
class HashSetSequential : public HashSetBase<T> {
public:
    explicit HashSetSequential(size_t capacity) : bucket_count_(capacity), elem_count_(0) {
        table_.resize(bucket_count_);
        for (size_t i = 0; i < bucket_count_; i++) {
            table_[i] = std::vector<T>();
        }
    }

    bool Add(T elem) final {
        if (Contains(elem)) return false;
        size_t my_bucket = std::hash<T>()(elem) % bucket_count_;
        table_[my_bucket].push_back(elem);
        elem_count_++;
        if (Policy()) {
            Resize();
        }
        return true;
    }

    bool Remove(T elem) final {
        if (!Contains(elem)) return false;
        size_t my_bucket = std::hash<T>()(elem) % bucket_count_;
        std::vector<T> small_table = table_[my_bucket];
        small_table.erase(std::remove(small_table.begin(), small_table.end(), elem), small_table.end());
        elem_count_--;
        return true;
    }

    [[nodiscard]] bool Contains(T elem) final {
        size_t my_bucket = std::hash<T>()(elem) % bucket_count_;
        std::vector<T> small_table = table_[my_bucket];
        return std::find(small_table.begin(), small_table.end(), elem) != small_table.end();
    }

    [[nodiscard]] size_t Size() const final {
        return elem_count_;
    }


private:

    std::vector<std::vector<T>> table_;
    size_t bucket_count_;
    size_t elem_count_;

    bool Policy() {
        return (elem_count_ / bucket_count_) > 4;
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

#endif  // HASH_SET_SEQUENTIAL_H
