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


/*
 * The refinable hash set implemented mainly follows the Art of Multiprocessor Programming
 * with some changes. Instead of having an atomic markable reference which C++
 * does not natively support, we decided to have a shared mutex, in order to allow
 * any non-resizable operations to occur the same as striped, but when resizing, the threads
 * are unable to acquire any of the mutexes in the mutex vector. Another major change is in the
 * Quiesce function, since C++ does not have IsLocked function on mutexes, we just get the
 * ScopedLock for every mutex on the mutex vector to ensure that no thread has any of the mutexes
 * allowing for the mutex vector to be resized safely.
 */
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

  // When adding an element we use our custom scoped lock to acquire the correct
  // mutex for this bucket so no other operations can be made on it at the same
  // time then we resize if the policy function returns true.
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

  // When removing an element we use our custom scoped lock to acquire the
  // correct mutex for this bucket so no other operations can be made on it at
  // the same time.
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
  // When checking for an element we use our custom scoped lock to acquire the
  // correct mutex for this bucket.
  [[nodiscard]] bool Contains(T elem) final {
    CustomScopedLock customScopedLock(this, elem);
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
    bool res = ContainsNoLock(elem, my_bucket);
    return res;
  }

  [[nodiscard]] size_t Size() const final { return elem_count_.load(); }

private:
  std::vector<std::vector<T>> table_;
  // We ensure that the element count is right by making it an atomic variable.
  std::atomic<size_t> bucket_count_;
  // The element count must be atomic, so that we can have multiple operations
  // on separate buckets.
  std::atomic<size_t> elem_count_;
  // A vector of mutexes for each bucket, which gets resized together with the
  // buckets vector.
  std::vector<std::mutex> mutexes_;
  // We use a shared lock for resizing, so we are able to allow threads that are
  // not resizing to be able to share the mutex.
  std::shared_mutex resizing_mutex_;

  bool ContainsNoLock(T elem, size_t my_bucket) {
    std::vector<T> small_table = table_[my_bucket];
    return std::find(small_table.begin(), small_table.end(), elem) !=
           small_table.end();
  }

  // Policy to resize, calculated using the atomic variable for element and
  // bucket counts.
  bool Policy() { return (elem_count_.load() / bucket_count_.load()) > 4; }

  /*
   * When resizing, we lock the resizing mutex to ensure that no other threads
   * can start resizing after that we also use quiesce to ensure that every
   * thread has released all the mutexes they are using. Then, we resize the new
   * vector of mutexes and the table.
   */
  void Resize() {
    size_t old_capacity = bucket_count_.load();
    std::lock_guard<std::shared_mutex> writer_lock(resizing_mutex_);
    size_t new_capacity = 2 * old_capacity;
    // This is to ensure we are not resizing more than once at a time.
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
  // The quiesce function acquires all locks, so that it ensures that mutexes
  // are free.
  void Quiesce() {
    for (std::mutex &mutex : mutexes_) {
      std::scoped_lock<std::mutex> lock(mutex);
    }
  }

  /*
   * We use a custom acquire function to lock the corresponding mutex for each
   * bucket given the element to search for. It also acquires a reader lock to
   * let any non-resizing operation occur concurrently.
   */
  void Acquire(T elem) {
    std::shared_lock<std::shared_mutex> reader_lock(resizing_mutex_);
    size_t my_bucket = std::hash<T>()(elem) % bucket_count_.load();
    mutexes_[my_bucket].lock();
  }

  // Custom release function for the mutex of the bucket corresponding to the
  // passed elem argument.
  void Release(T elem) {
    mutexes_[std::hash<T>()(elem) % bucket_count_.load()].unlock();
  }

  // Auxiliary class that creates a scoped lock, using the custom acquire
  // function.
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
