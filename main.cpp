#include "queue.h"
#include "smart_pointer.h"
#include "stack.h"
#include <print>
#include <thread>

AtomicQueue<int> data_structure{};

void producer() {
  int i = 500;
  while (i > 0) {
    std::println("Producing");
    data_structure.push(&i);
    i--;
  }
}

void consumer() {
  int capacityLeft = 250;
  while (capacityLeft > 0) {
    int res;
    if (data_structure.pop(&res)) {
      std::println("Consuming {}", std::this_thread::get_id());
      capacityLeft--;
    }
  }
}
int main() {
  auto producerThread = std::thread(producer);
  std::vector<std::thread> consumerThreads;
  for (int i = 0; i < 2; ++i) {
    consumerThreads.push_back(std::thread(consumer));
  }
  producerThread.join();
  for (auto &consumerThread : consumerThreads) {
    consumerThread.join();
  }
  return 0;
}
