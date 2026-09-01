#include <print>
#include <stack>
#include <thread>
#include <shared_mutex>
#include <unistd.h>
#include <condition_variable>
#include <vector>

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
        is_stack_nonempty.wait(lk, [this]{return !s.empty();});
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

auto stack = ConcurrentStack();

void producer() {
    int i = 20;
    while (i > 0) {
        std::println("Producing");
        stack.push(i);
        i--;
        sleep(1);
    }
}

void consumer() {
    int capacityLeft = 10;
    while (capacityLeft > 0) {
        if (stack.size() > 0) {
            std::println("Consuming {}", std::this_thread::get_id());
            stack.wait_and_pop();
            capacityLeft--;
        }
    }
}

int main() {
    std::println("Hello, World!");
    auto producerThread = std::thread(producer);
    std::vector<std::thread> consumerThreads;
    for (int i = 0; i < 2; ++i) {
      consumerThreads.push_back(std::thread(consumer));
    }
    producerThread.join();
    for (auto& consumerThread: consumerThreads) {
      consumerThread.join();
    }
    return 0;
}
