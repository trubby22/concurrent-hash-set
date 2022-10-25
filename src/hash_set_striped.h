#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <deque>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"
#include "src/scoped_vector_lock.h"

template <typename T> class HashSetStriped : public HashSetBase<T> {
public:
  explicit HashSetStriped(size_t capacity)
      : bucket_count_(capacity), initial_bucket_count_(capacity),
        elem_count_(0) {
    table_.resize(bucket_count_.load());
    mutexes_ = std::vector<std::mutex>(initial_bucket_count_);
    for (size_t i = 0; i < bucket_count_.load(); i++) {
      table_[i] = std::vector<T>();
    }
  }

  // we find the mutex corresponding to our bucket and then we lock it so no one else accesses
  // the same bucket
  bool Add(T elem) final {
    size_t my_lock = std::hash<T>()(elem) % initial_bucket_count_;
    {
      std::scoped_lock<std::mutex> lock(mutexes_[my_lock]);
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

  // when removing an element we apply the same principle we did for the add operation
  // so we lock the correct mutex remove the element from the bucket
  bool Remove(T elem) final {
    size_t my_lock = std::hash<T>()(elem) % initial_bucket_count_;
    std::scoped_lock<std::mutex> lock(mutexes_[my_lock]);
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
    if (!ContainsNoLock(elem, my_bucket))
      return false;
    assert(elem_count_ != 0);
    table_[my_bucket].erase(
        std::find(table_[my_bucket].begin(), table_[my_bucket].end(), elem));
    elem_count_.fetch_sub(1);
    return true;
  }

  // for the contains operation we again lock the corresponding mutex and search the bucket
  [[nodiscard]] bool Contains(T elem) final {
    size_t my_lock = std::hash<T>()(elem) % initial_bucket_count_;
    std::scoped_lock<std::mutex> lock(mutexes_[my_lock]);
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
    return ContainsNoLock(elem, my_bucket);
  }

  [[nodiscard]] size_t Size() const final { return elem_count_.load(); }

private:
  std::vector<std::vector<T>> table_;
  // we ensure that the bucket count is right by making it an atomic variable
  std::atomic<size_t> bucket_count_;
  // we keep track of the initial bucket count because the vector of mutexes does not resize
  size_t initial_bucket_count_;
  // the element count must be atomic, so that we can have multiple operations on separate buckets
  std::atomic<size_t> elem_count_;
  // a vector of mutexes for each initial bucket at first
  // after resizing more buckets will share the same lock
  std::vector<std::mutex> mutexes_;

  bool ContainsNoLock(T elem, size_t my_bucket) {
    std::vector<T> small_table = table_[my_bucket];
    return std::find(small_table.begin(), small_table.end(), elem) !=
           small_table.end();
  }

  bool Policy() { return (elem_count_.load() / bucket_count_.load()) > 4; }

  // when resizing we have to stop all other operations so we first lock the whole vector of mutexes
  // using our custom scoped vector lock
  void Resize() {
    size_t old_capacity = bucket_count_.load();
    ScopedVectorLock scopedLockVector(mutexes_);
    if (old_capacity != bucket_count_.load()) {
      return;
    }
    size_t new_capacity = 2 * old_capacity;
    bucket_count_.store(new_capacity);
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

#endif // HASH_SET_STRIPED_H
