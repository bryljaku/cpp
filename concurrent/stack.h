#include <condition_variable>
#include <shared_mutex>
#include <stack>
#include <unistd.h>

class ConcurrentStack {
public:
  ConcurrentStack() = default;

  void push(int value) {
    {
      auto lk = std::lock_guard(m);
      s.push(value);
    }
    is_stack_nonempty.notify_one();
  }

  int wait_and_pop() {
    int res;
    {
      auto lk = std::unique_lock(m);
      is_stack_nonempty.wait(lk, [this] { return !s.empty(); });
      res = s.top();
      s.pop();
    }
    return res;
  }

  size_t size() const {
    auto lk = std::shared_lock(m);
    return s.size();
  }

private:
  std::stack<int> s;
  mutable std::shared_mutex m;
  std::condition_variable_any is_stack_nonempty;
};
