#include <cstring>
#include "test.h"
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failed = 0;
    for (auto& t : registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        int before = failures();
        t.fn();
        ++ran;
        bool ok = failures() == before;
        if (!ok) ++failed;
        std::printf("[%s] %s\n", ok ? " ok " : "FAIL", t.name);
    }
    std::printf("\n%d tests, %d failed\n", ran, failed);
    return failed ? 1 : 0;
}
