#include <condition_variable>
#include <queue>
#include <shared_mutex>

template <typename T> class SimpleQueue {
public:
  SimpleQueue() {};

  SimpleQueue(SimpleQueue &) = delete;
  SimpleQueue &operator=(SimpleQueue &) = delete;

  bool pop(T *target) {
    auto lk = std::unique_lock(m);
    if (internal_q.empty()) {
      return false;
    }
    *target = std::move(internal_q.front());
    internal_q.pop();
    return true;
  };

  void push(T *value) {
    auto lk = std::unique_lock(m);
    internal_q.push(*value);
  };

  bool empty() const {
    auto lk = std::unique_lock(m);
    return internal_q.size();
  };

private:
  std::queue<T> internal_q;
  mutable std::shared_mutex m;
};

// condition variable and wait_and_pop
template <typename T> class SimpleMutexQueue {
public:
  SimpleMutexQueue() {};

  SimpleMutexQueue(SimpleMutexQueue &) = delete;
  SimpleMutexQueue &operator=(SimpleMutexQueue &) = delete;

  T wait_and_pop() {
    T res;
    {
      auto lk = std::unique_lock(m);
      cond.wait(lk, [this]() { return !internal_q.empty(); });
      res = internal_q.back();
      internal_q.pop();
    }
    return res;
  };

  void push(T value) {
    {
      auto lk = std::lock_guard(m);
      internal_q.push(value);
    }
    cond.notify_all();
  };

  bool empty() const {
    auto lk = std::shared_lock(m);
    return internal_q.size();
  }

private:
  std::queue<T> internal_q;
  mutable std::shared_mutex m;
  std::condition_variable_any cond;
};
