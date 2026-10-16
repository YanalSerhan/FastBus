// TSan canary: two threads increment a plain int without synchronization.
// TSan reports the race and, with halt_on_error=1 or by default at exit, returns non-zero.
#include <cstdio>
#include <thread>

namespace {
int counter = 0;  // deliberately not atomic

void bump() {
    for (int i = 0; i < 100000; ++i) {
        ++counter;
    }
}
}  // namespace

int main() {
    std::thread a(bump);
    std::thread b(bump);
    a.join();
    b.join();
    std::printf("TSan canary survived (counter=%d): ThreadSanitizer is NOT active\n", counter);
    return 0;
}
