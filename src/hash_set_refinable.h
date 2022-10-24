#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <vector>
#include <mutex>
#include <vector>
#include <string>
#include <sstream>
#include <thread>

#include "src/hash_set_base.h"

template <typename T> class HashSetRefinable : public HashSetBase<T> {
public:
  explicit HashSetRefinable(size_t capacity)
      : bucket_count_(capacity), elem_count_(0) {
    table_.resize(bucket_count_.load());
    mutexes_ = std::vector<std::mutex>(bucket_count_.load());
    for (size_t i = 0; i < bucket_count_.load(); i++) {
      table_[i] = std::vector<T>();
    }
  }

  bool Add(T elem) final {
    Acquire(elem);
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
    if (ContainsNoLock(elem, my_bucket)) {
      Release(elem);
      return false;
    }
    table_[my_bucket].push_back(elem);
    elem_count_.fetch_add(1);
    Release(elem);
    if (Policy()) {
      Resize();
    }
    return true;
  }

  bool Remove(T elem) final {
    Acquire(elem);
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
    if (!ContainsNoLock(elem, my_bucket)) {
      Release(elem);
      return false;
    }
    assert(elem_count_ != 0);
    table_[my_bucket].erase(
        std::find(table_[my_bucket].begin(), table_[my_bucket].end(), elem));
    elem_count_.fetch_sub(1);
    Release(elem);
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    Acquire(elem);
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
    bool res = ContainsNoLock(elem, my_bucket);
    Release(elem);
    return res;
  }

  [[nodiscard]] size_t Size() const final { return elem_count_.load(); }

private:
  std::vector<std::vector<T>> table_;
  std::atomic<size_t> bucket_count_;
  std::atomic<size_t> elem_count_;
  std::vector<std::mutex> mutexes_;
  std::mutex resizing_lock;

  bool ContainsNoLock(T elem, size_t my_bucket) {
    std::vector<T> small_table = table_[my_bucket];
    return std::find(small_table.begin(), small_table.end(), elem) !=
           small_table.end();
  }

  bool Policy() { return (elem_count_.load() / bucket_count_.load()) > 4; }

  void Resize() {
    size_t old_capacity = bucket_count_.load();
    resizing_lock.lock();
    size_t new_capacity = 2 * old_capacity;
    if (bucket_count_.load() != old_capacity) {
      resizing_lock.unlock();
      return;
    }
    Quiesce();
    bucket_count_.store(new_capacity);
    mutexes_ = std::vector<std::mutex>(bucket_count_.load());
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
    resizing_lock.unlock();
    return;
  }

  void Quiesce() {
    for (std::mutex& m : mutexes_) {
      m.lock();
    }
    for (std::mutex& m : mutexes_) {
      m.unlock();
    }
  }

  void Acquire(T elem) {
    resizing_lock.lock();
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
    mutexes_[my_bucket].lock();
    resizing_lock.unlock();
  }

  void Release(T elem) {
    mutexes_[std::hash<T>()(elem) % bucket_count_.load()].unlock();
  }

};

#endif // HASH_SET_REFINABLE_H
