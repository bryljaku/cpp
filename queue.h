#include <algorithm>
#include <cassert>
#include <concepts>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <queue>
#include <shared_mutex>
#include <type_traits>

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif

template <typename T, uint32_t Alignment> struct alignas(Alignment) Aligned {
public:
  template <typename... Args>
  FORCE_INLINE constexpr Aligned(Args &&...args)
      : data(std::forward<Args>(args)...) {}

  FORCE_INLINE operator T &() noexcept { return data; }
  FORCE_INLINE operator const T &() const noexcept { return data; }
  T *operator->() noexcept { return &data; }
  const T *operator->() const noexcept { return &data; }

private:
  T data;
};

template <typename F>
  requires(std::is_invocable_v<F>)
class Defer {
public:
  explicit Defer(F payload) : deferredPayload(payload) {};
  ~Defer() { std::invoke(deferredPayload); };

private:
  F deferredPayload;
};

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)
#define DEFER(...)                                                             \
  const auto CONCAT(__deferedPayload, __LINE__) = Defer([&]() { __VA_ARGS__; });

template <typename T, uint32_t PointerCount>
struct alignas(64) HazardThreadState {
  std::array<std::atomic<T *>, PointerCount> hazardPointers;
  std::vector<T *> retiredPointers;
  std::vector<T *> scratchBuffer;
};

template <typename T, uint32_t PointerCount, uint32_t ThreadCount,
          uint32_t RetireThreshold = 2 * PointerCount * ThreadCount>
  requires(ThreadCount > 0)
class HazardPointer {
public:
  HazardPointer() {
    for (auto &threadState : states) {
      for (auto &hp : threadState.hazardPointers) {
        hp.store(nullptr, std::memory_order_relaxed);
      }
      threadState.retiredPointers.reserve(ThreadCount * PointerCount);
      threadState.scratchBuffer.reserve(ThreadCount * PointerCount * 4);
      threadState.retiredPointers.clear();
      threadState.scratchBuffer.clear();
    }
  }
  ~HazardPointer() {
    for (auto &threadState : states) {
      assert(std::count_if(
                 threadState.hazardPointers.begin(),
                 threadState.hazardPointers.end(), [](const auto &ptr) {
                   return ptr.load(std::memory_order::relaxed) != nullptr;
                 }) == 0);
      for (auto &ptr : threadState.retiredPointers) {
        if (ptr) {
          delete ptr;
        };
      }
    }
  }

  T *protect(std::atomic<T *> &ptr, uint32_t threadId, uint32_t slotId) {
    assert(threadId < ThreadCount);
    assert(slotId < PointerCount);

    T *p;
    do {
      p = ptr.load(std::memory_order::acquire);
      states[threadId].hazardPointers[slotId].store(p,
                                                    std::memory_order::release);
    } while (p && p != states[threadId].hazardPointers[slotId].load(
                           std::memory_order::seq_cst));

    return p;
  }

  void release(uint32_t threadId, uint32_t slotId) {
    states[threadId].hazardPointers[slotId].store(nullptr,
                                                  std::memory_order::release);
  }

  void clear(uint32_t threadId) {
    for (int slotId = 0; slotId < PointerCount; ++slotId) {
      release(threadId, slotId);
    }
  }

  template <typename Deleter = std::nullptr_t>
    requires(std::is_invocable_v<Deleter, T *> ||
             std::same_as<Deleter, std::nullptr_t>)
  void retire(T *ptr, uint32_t threadId, Deleter deleter = nullptr) {
    states[threadId].retiredPointers.push_back(ptr);
    if (states[threadId].retiredPointers.size() >= RetireThreshold) {
      scan(threadId, deleter);
    }
  }

  template <typename Deleter = std::nullptr_t>
    requires(std::is_invocable_v<Deleter, T *> ||
             std::same_as<Deleter, std::nullptr_t>)
  void scan(uint32_t threadId, Deleter deleter) {
    auto &activePointers = states[threadId].scratchBuffer;
    activePointers.clear();

    for (auto &threadState : states) {
      for (auto &p : threadState.hazardPointers) {
        auto ptr = p.load(std::memory_order::seq_cst);
        if (ptr)
          activePointers.push_back(ptr);
      }
    }
    std::sort(activePointers.begin(), activePointers.end());

    auto &retiredList = states[threadId].retiredPointers;

    for (int i = 0; i < retiredList.size();) {
      if (!std::binary_search(activePointers.begin(), activePointers.end(),
                              retiredList[i])) {
        if constexpr (std::same_as<Deleter, std::nullptr_t>) {
          deleter(retiredList[i]);
        } else {
          delete retiredList[i];
        }
        retiredList[i] = retiredList.back();
        retiredList.pop_back();
      } else {
        ++i;
      }
    }
  }

private:
  std::array<HazardThreadState<T, PointerCount>, ThreadCount> states;
};

template <typename T, bool BlockingRead>
  requires std::is_trivially_copyable_v<T>
class FastQueueNodeSlot {
public:
  FastQueueNodeSlot() { committed.clear(std::memory_order::relaxed); };
  void commit(T *value) {
    memcpy(&data, value, sizeof(T));
    committed.test_and_set(std::memory_order::release);
    if constexpr (BlockingRead) {
      committed.notify_one();
    }
  }
  bool isCommitted() const {
    return committed.test(std::memory_order::acquire);
  }
  void waitTillCommitted() const {
    while (!isCommitted()) {
      committed.wait(false, std::memory_order::relaxed);
    }
  }
  bool tryToRead(T *target) const {
    if (isCommitted()) {
      memcpy(&target, &data, sizeof(T));
      return true;
    }
    return false;
  }
  void read(T *target) const {
    waitTillCommitted();
    memcpy(&target, &data, sizeof(T));
  }
  void reset() { committed.clear(std::memory_order::release); }

private:
  T data;
  std::atomic_flag committed;
};

template <typename T, uint32_t Size, bool BlockingRead> class FastQueueNode {
public:
  using FastQN = FastQueueNode<T, Size, BlockingRead>;

  FastQueueNode() { reset(); }

  bool push(T *value) {
    auto oldHead = headIndex.load(std::memory_order::acquire);
    while (true) {
      if (oldHead == Size) {
        return false;
      }
      auto newHead = oldHead + 1;
      if (headIndex.compare_exchange_strong(oldHead, newHead,
                                            std::memory_order::seq_cst,
                                            std::memory_order::acquire)) {
        slots[oldHead].commit(value);
        return true;
      }
    }
  }

  bool pop(T *target) {
    auto currentTail = tailIndex.load(std::memory_order::seq_cst);
    while (true) {
      auto currentHead = headIndex.load(std::memory_order::acquire);
      if (currentTail >= currentHead) {
        return false;
      }
      if constexpr (!BlockingRead) {
        if (!slots[currentTail].isCommitted()) {
          return false;
        }
      }
      auto newTail = currentTail + 1;
      if (tailIndex.compare_exchange_strong(currentTail, newTail,
                                            std::memory_order::seq_cst,
                                            std::memory_order::acquire)) {
        slots[currentTail].read(target);
        return true;
      }
    }
    return false;
  }

  FastQN *getNext(std::memory_order order) const { return next.load(order); }

  bool trySetNext(FastQN *&expected, FastQN *value) {
    return next.compare_exchange_strong(expected, value,
                                        std::memory_order::seq_cst,
                                        std::memory_order::acquire);
  }

  bool isUsedUp(std::memory_order order) const {
    return tailIndex.load(order) >= Size;
  }

  bool isEmpty(std::memory_order order) const {
    return tailIndex.load(order) >= headIndex.load(order);
  }

  void reset() {
    tailIndex.store(0, std::memory_order::release);
    headIndex.store(0, std::memory_order::release);
    next.store(nullptr, std::memory_order::release);
    for (auto &slot : slots) {
      slot.reset();
    }
  }

private:
  alignas(64) std::array<FastQueueNodeSlot<T, BlockingRead>, Size> slots;
  alignas(64) std::atomic<uint32_t> headIndex;
  alignas(64) std::atomic<uint32_t> tailIndex;
  alignas(64) std::atomic<FastQN *> next;
};

template <typename T, uint32_t NodeBufferSize, uint32_t ThreadCount,
          bool BlockingRead = false, uint32_t ThreadLocalCacheSize = 256>
class FastQueue {
public:
  using Node = FastQueueNode<T, BlockingRead, NodeBufferSize>;
  using Self = FastQueue<T, NodeBufferSize, ThreadCount, BlockingRead,
                         ThreadLocalCacheSize>;

  FastQueue() {
    auto node = new Node();
    tail.store(node, std::memory_order::relaxed);
    head.store(node, std::memory_order::relaxed);
    if constexpr (ThreadCount > 0) {
      for (int i = 0; i < ThreadCount; ++i) {
        threadLocalCacheBuffers[i]->reserve(ThreadLocalCacheSize);
      }
    }
  };

  ~FastQueue() {
    auto current = tail.load(std::memory_order::relaxed);
    while (current != nullptr) {
      auto next = current->getNext(std::memory_order::relaxed);
      delete current;
      current = next;
    }
    if constexpr (ThreadCount > 0) {
      for (int threadId = 0; threadId < ThreadCount; ++threadId) {
        for (auto &node :
             (ThreadLocalCacheBuffer)threadLocalCacheBuffers[threadId]) {
          delete node;
        }
      }
    }
  }

  void push(T *value, uint32_t threadId) {
    Node *current = nullptr, *next = nullptr;
    while (true) {
      current = hazardPointerManager.protect(head, threadId, 0);
      DEFER(hazardPointerManager.release(threadId, 0));
      if (current->push(value)) {
        break;
      }
      next = current->getNext(std::memory_order::acquire);
      if (next == nullptr) {
        auto newNode = acquireNewNode(threadId);
        if (!current->trySetNext(next, newNode)) {
          releaseNode(newNode, threadId);
        }
        continue;
      }
      head.compare_exchange_strong(current, next, std::memory_order::seq_cst,
                                   std::memory_order::acquire);
    }
  };

  bool pop(T *value, uint32_t threadId) {
    Node *currentTail = nullptr, *next = nullptr;
    while (true) {
      auto currentTail = hazardPointerManager.protect(tail, threadId, 1);
      DEFER(hazardPointerManager.release(threadId, 1));
      if (currentTail->pop(value)) {
        return true;
      }
      if (!currentTail->isUsedUp(std::memory_order::acquire)) {
        return false;
      }
      next = currentTail->getNext(std::memory_order::acquire);
      if (next == nullptr) {
        return false;
      }
      if (tail.compare_exchange_strong(currentTail, next,
                                       std::memory_order::seq_cst,
                                       std::memory_order::acquire)) {
        hazardPointerManager.retire(currentTail, threadId,
                                    [this, threadId](Node *node) {
                                      this->releaseNode(node, threadId);
                                    });
      }
    }
  };

  bool empty() const { /* to fix thread safety*/
    return head != tail || tail.empty();
  }

private:
  Node *acquireNewNode(uint32_t threadId) {
    if constexpr (ThreadCount > 0) {
      if (threadLocalCacheBuffers[threadId]->size() > 0) {
        auto ret = threadLocalCacheBuffers[threadId]->back();
        threadLocalCacheBuffers[threadId]->pop_back();
        ret->reset();
        return ret;
      }
    }
    return new Node();
  }
  void releaseNode(Node *ptr, uint32_t threadId) {
    if constexpr (ThreadCount > 0) {
      if (threadLocalCacheBuffers[threadId]->size() < ThreadLocalCacheSize) {
        threadLocalCacheBuffers[threadId]->push_back(ptr);
        return;
      }
    }
    delete ptr;
  }

  using ThreadLocalCacheBuffer = std::vector<Node *>;

  std::atomic<Node *> head;
  std::atomic<Node *> tail;
  HazardPointer<Node, ThreadCount, 4, 16> hazardPointerManager;
  std::array<Aligned<ThreadLocalCacheBuffer, 64>,
             (ThreadLocalCacheSize > 0) ? ThreadCount : 1>
      threadLocalCacheBuffers;
};

// Naive lock free which is not cache friendly and not necessarily safe

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
