// UBSan canary: signed integer overflow. Must abort under UBSan (-fno-sanitize-recover=all).
#include <climits>
#include <cstdio>

int main() {
    volatile int value = INT_MAX;  // volatile: prevent constant folding
    const int overflowed = value + 1;
    std::printf("UBSan canary survived (%d): UndefinedBehaviorSanitizer is NOT active\n",
                overflowed);
    return 0;
}
