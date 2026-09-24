#include <condition_variable>
#include <queue>
#include <shared_mutex>

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif

template <typename T> struct NaiveNode {
  T data;
  std::atomic<NaiveNode *> next;
};

template <typename T> class AtomicQueue {
public:
  AtomicQueue() {
    auto dummy = new NaiveNode<T>();
    dummy->next.store(nullptr);
    tail.store(dummy);
    head.store(dummy);
  };

  AtomicQueue(AtomicQueue &) = delete;
  AtomicQueue &operator=(AtomicQueue &) = delete;
  AtomicQueue(AtomicQueue &&) = delete;
  AtomicQueue &operator=(AtomicQueue &&) = delete;

  FORCE_INLINE void push(T *value, uint32_t threadId) {
    auto newNode = new NaiveNode<T>();
    newNode->next.store(nullptr, std::memory_order::relaxed);
    newNode->data = *value;

    while (true) {
      auto currentHead = head.load(std::memory_order::acquire);
      auto currentNext = currentHead->next.load(std::memory_order::acquire);

      if (currentHead != head.load(std::memory_order::acquire)) {
        continue;
      }
      if (currentNext == nullptr) {
        NaiveNode<T> *expected;
        if (currentHead->next.compare_exchange_strong(expected, newNode)) {
          head.compare_exchange_strong(currentHead, newNode);
          return;
        }
      } else {
        head.compare_exchange_strong(currentHead, currentNext);
      }
    }
  }

  bool pop(T *target, uint32_t threadId) {

    while (true) {
      auto currentTail = tail.load(std::memory_order::acquire);
      auto currentHead = head.load(std::memory_order::acquire);
      auto currentTailNext = currentTail->next.load(std::memory_order::acquire);
      if (currentTail != tail.load(std::memory_order::acquire)) {
        continue;
      }
      if (currentTail == currentHead) {
        if (currentTailNext == nullptr) {
          return false;
        } else {
          head.compare_exchange_strong(currentHead, currentTailNext);
        }
      } else {
        if (currentTailNext == nullptr) {
          continue;
        } else {
          *target = currentTailNext->data;
          if (tail.compare_exchange_strong(currentTail, currentTailNext)) {
            delete currentTail;
            return true;
          }
        }
      }
    }
  };

private:
  std::atomic<NaiveNode<T> *> head;
  std::atomic<NaiveNode<T> *> tail;
};

template <typename T> class SimpleQueue {
public:
  SimpleQueue() {};

  SimpleQueue(SimpleQueue &) = delete;
  SimpleQueue &operator=(SimpleQueue &) = delete;

  SimpleQueue(SimpleQueue &&) = delete;
  SimpleQueue &operator=(SimpleQueue &&) = delete;

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
  mutable std::mutex m;
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
