#include <print>
#include <stack>
#include <thread>

class ConcurrentStack {
    public:
    ConcurrentStack() = default;
    
    void push(int value) {
        s.push(value);
    }
    
    void pop() {
        if (!s.empty()) {
            s.pop();
        }
    }
    
    int top() const {
        if (!s.empty()) {
            return s.top();
        }
        throw std::runtime_error(
            "Cannot pop from an empty stack"
        );
    }
    
    size_t size() const {
        return s.size();
    }


    private:
    std::stack<int> s;
};

auto stack = ConcurrentStack();

void producer() {
    int i = 10;
    while (i > 0) {
        std::println("Producing");
        stack.push(i);
        i--;
    }
}

void consumer() {
    int i = 10;
    while (i > 0) {
        if (stack.size() > 0) {
            std::println("Consuming");
            stack.pop();
            i--;
        }
    }
}

int main() {
    std::println("Hello, World!");
    auto producerThread = std::thread(producer);
    auto consumerThread = std::thread(consumer);
    producerThread.join();
    consumerThread.join();
    return 0;
}
