#ifndef HASH_SET_COARSE_GRAINED_H
#define HASH_SET_COARSE_GRAINED_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

/*
 * The coarse grained implementation of the concurrent hashset is mainly a
 * straightforward solution that involves utilizing a scoped lock on the same
 * mutex for all operations. Since this does not support reentrancy, we made a
 * special function named ContainsNoLock so that it can be used on add and
 * remove. Bucket_count_ is atomic due to the reason that Policy now occurs when
 * threads have no locks, so there is a potential data race in that specific
 * point. Atomicity makes sure that the value can be updated in one step,
 * avoiding the potential data race.
 */
template <typename T> class HashSetCoarseGrained : public HashSetBase<T> {
public:
  explicit HashSetCoarseGrained(size_t capacity)
      : bucket_count_(capacity), elem_count_(0) {
    table_.resize(bucket_count_);
    for (size_t i = 0; i < bucket_count_; i++) {
      table_[i] = std::vector<T>();
    }
  }

  // We use a scoped lock to ensure that the buckets are not changed during the
  // add function.
  bool Add(T elem) final {
    std::scoped_lock<std::mutex> lock(mutex_);
    if (ContainsNoLock(elem))
      return false;
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_;
    table_[my_bucket].push_back(elem);
    elem_count_.fetch_add(1);
    if (Policy()) {
      Resize();
    }
    return true;
  }

  // We lock the global mutex until the remove command is finished.
  bool Remove(T elem) final {
    std::scoped_lock<std::mutex> lock(mutex_);
    if (!ContainsNoLock(elem))
      return false;
    assert(elem_count_ != 0);
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_;
    table_[my_bucket].erase(
        std::find(table_[my_bucket].begin(), table_[my_bucket].end(), elem));
    elem_count_.fetch_sub(1);
    return true;
  }

  // We lock the global mutex to ensure that the hash set is not changed during
  // the check function.
  [[nodiscard]] bool Contains(T elem) final {
    std::scoped_lock<std::mutex> lock(mutex_);
    return ContainsNoLock(elem);
  }

  [[nodiscard]] size_t Size() const final { return elem_count_.load(); }

private:
  std::vector<std::vector<T>> table_;
  size_t bucket_count_;
  // We ensure that the element count is right by making it an atomic variable.
  std::atomic<size_t> elem_count_;
  // We have a single mutex for all operations of the hash set.
  std::mutex mutex_;

  bool ContainsNoLock(T elem) {
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_;
    std::vector<T> small_table = table_[my_bucket];
    return std::find(small_table.begin(), small_table.end(), elem) !=
           small_table.end();
  }

  bool Policy() { return (elem_count_.load() / bucket_count_) > 4; }

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

#endif // HASH_SET_COARSE_GRAINED_H
