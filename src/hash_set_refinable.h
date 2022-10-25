#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "src/hash_set_base.h"
#include "src/scoped_vector_lock.h"

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
    {
      CustomScopedLock customScopedLock(this, elem);
      size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
      if (ContainsNoLock(elem, my_bucket)) {
        return false;
      }
      table_[my_bucket].push_back(elem);
      elem_count_.fetch_add(1);
    }
    if (Policy()) {
      Resize();
    }
    return true;
  }

  bool Remove(T elem) final {
    CustomScopedLock customScopedLock(this, elem);
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
    if (!ContainsNoLock(elem, my_bucket)) {
      return false;
    }
    assert(elem_count_ != 0);
    table_[my_bucket].erase(
        std::find(table_[my_bucket].begin(), table_[my_bucket].end(), elem));
    elem_count_.fetch_sub(1);
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    CustomScopedLock customScopedLock(this, elem);
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
    bool res = ContainsNoLock(elem, my_bucket);
    return res;
  }

  [[nodiscard]] size_t Size() const final { return elem_count_.load(); }

private:
  std::vector<std::vector<T>> table_;
  std::atomic<size_t> bucket_count_;
  std::atomic<size_t> elem_count_;
  std::vector<std::mutex> mutexes_;
  std::shared_mutex resizing_mutex_;

  bool ContainsNoLock(T elem, size_t my_bucket) {
    std::vector<T> small_table = table_[my_bucket];
    return std::find(small_table.begin(), small_table.end(), elem) !=
           small_table.end();
  }

  bool Policy() { return (elem_count_.load() / bucket_count_.load()) > 4; }

  void Resize() {
    size_t old_capacity = bucket_count_.load();
    std::lock_guard<std::shared_mutex> writer_lock(resizing_mutex_);
    size_t new_capacity = 2 * old_capacity;
    if (bucket_count_.load() != old_capacity) {
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
  }

  void Quiesce() {
    for (std::mutex &mutex : mutexes_) {
      mutex.lock();
      mutex.unlock();
    }
  }

  void Acquire(T elem) {
    std::shared_lock<std::shared_mutex> reader_lock(resizing_mutex_);
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
    mutexes_[my_bucket].lock();
  }

  void Release(T elem) {
    mutexes_[std::hash<T>()(elem) % bucket_count_.load()].unlock();
  }

  class CustomScopedLock {
  public:
    CustomScopedLock(HashSetRefinable<T> *hashSetRefinable, T elem)
        : hashSetRefinable_(hashSetRefinable), elem_(elem) {
      hashSetRefinable_->Acquire(elem_);
    }
    ~CustomScopedLock() { hashSetRefinable_->Release(elem_); }

  private:
    HashSetRefinable<T> *hashSetRefinable_;
    T elem_;
  };
};

#endif // HASH_SET_REFINABLE_H
