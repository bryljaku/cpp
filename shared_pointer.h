#include <print> 
#include <unistd.h> 


// Responsibility of a smart pointer 
// 1. Free up the memory after no one is using the object anymore => call free in the destructor. RAII 

template <typename T> 
class shared_pointer {
  struct shared_pointer_metadata {
    int ref_count;

  };

 public:
    // shared_pointer() : ptr(nullptr), md(new shared_pointer_metadata(0)) {};
    shared_pointer(T* ptr) : ptr(ptr), md(new shared_pointer_metadata(1)) {};

    // copy
    shared_pointer(const shared_pointer<T>& sp) {
      std::println("copy");
      this->ptr = sp.ptr;
      this->md = sp.md;
      this->md->ref_count++;
    };
    
    shared_pointer& operator=(const shared_pointer& sp) {
      std::println("copy assignment");
      cleanup_routine();
      this->ptr = sp.ptr;
      this->md = sp.md;

      this->md->ref_count++;
    }

    // move 
    shared_pointer(shared_pointer<T>&& sp) {
      std::println("move");
      this->ptr = sp.ptr;
      this->md = sp.md;
      sp.ptr = nullptr;
      sp.md = nullptr;
    };
    
    shared_pointer& operator=(shared_pointer<T>&& sp) {
      std::println("before cleanup routine");
      cleanup_routine();
      std::println("finished cleanup routine");
      this.ptr = sp.ptr;
      this.md = sp.md;
      sp.ptr = nullptr;
      sp.md = nullptr;
    };

    // destructor
    ~shared_pointer() {
      std::println("destructor");
     cleanup_routine();
    };

    int get_count() const {
      return md->ref_count;
    };

  private:
    shared_pointer(T* ptr, shared_pointer_metadata* md) : ptr(ptr), shared_pointer_metadata(md) {};

    void cleanup_routine() {
      if (md != nullptr) {
        std::println("decrementing count");
        md->ref_count--;
        if (md->ref_count <= 0) {
          if (ptr != nullptr) {
            std::println("clearing ptr");
            free(ptr);
          }
          std::println("clearing md");
          free(md);
        }
      }
    };

    T* ptr;
    shared_pointer_metadata* md;
};



template <typename T> 
class unique_pointer {
  public:
    unique_pointer(T* ptr) : ptr(ptr) {};
    
    // copy
    unique_pointer(const unique_pointer& obj) = delete;
    unique_pointer& operator=(const unique_pointer& obj) = delete;

    //move
    unique_pointer(unique_pointer&& obj) {
      this->ptr = obj.ptr;
      obj.ptr = nullptr;
    };
    
    unique_pointer& operator=(unique_pointer&& obj) {
      cleanup_routine();
      this->ptr = obj.ptr;
      obj.ptr = nullptr;
    };

    ~unique_pointer() {
      cleanup_routine();
    };

    bool is_set() const {
      return ptr != nullptr;
    };

  private:
    void cleanup_routine() {
      if (ptr != nullptr) {
        free(ptr);
      }
    };

    T* ptr;
};






