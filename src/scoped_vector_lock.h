//
// Created by Piotr Błaszyk on 24/10/2022.
//

#ifndef HASHSETS_SCOPED_VECTOR_LOCK_H
#define HASHSETS_SCOPED_VECTOR_LOCK_H

#include <mutex>
#include <vector>

class ScopedVectorLock {
public:
  explicit ScopedVectorLock(std::vector<std::mutex> &mutexes)
      : mutexes_(mutexes) {
    for (std::mutex &mutex : mutexes_) {
      mutex.lock();
    }
  }
  ~ScopedVectorLock() {
    for (std::mutex &mutex : mutexes_) {
      mutex.unlock();
    }
  }

private:
  std::vector<std::mutex> &mutexes_;
};

#endif // HASHSETS_SCOPED_VECTOR_LOCK_H
