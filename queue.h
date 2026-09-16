#include <condition_variable>
#include <queue>
#include <shared_mutex>

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
