// ASan canary: writes one element past the end of a heap array. Must abort under ASan.
#include <cstdio>
#include <memory>

int main() {
    auto data = std::make_unique<int[]>(4);
    volatile int index = 4;  // volatile: keep the optimizer from proving/removing the bug
    data[index] = 42;
    std::printf("ASan canary survived: AddressSanitizer is NOT active\n");
    return 0;
}
