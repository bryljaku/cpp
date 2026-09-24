#include "queue.h"
#include "smart_pointer.h"
#include "stack.h"
#include <print>
#include <thread>

AtomicQueue<int> data_structure{};

void producer(int id) {
  int i = 10;
  while (i > 0) {
    int y = i;
    std::println("Producing {} by thread {}", y, id);
    data_structure.push(&y, id);
    i--;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void consumer(int id) {
  int capacityLeft = 10;
  while (capacityLeft > 0) {
    int res;
    if (data_structure.pop(&res, id)) {
      std::println("Consumed {} by thread {}", res, id);
      capacityLeft--;
    }
  }
}
int main() {
  std::vector<std::thread> producerThreads;
  for (int i = 0; i < 2; ++i) {
    producerThreads.push_back(std::thread(producer, 1000 + i));
  }
  std::vector<std::thread> consumerThreads;
  for (int i = 0; i < 2; ++i) {
    consumerThreads.push_back(std::thread(consumer, i));
  }
  for (auto &producerThread : producerThreads) {
    producerThread.join();
  }
  for (auto &consumerThread : consumerThreads) {
    consumerThread.join();
  }
  return 0;
}
