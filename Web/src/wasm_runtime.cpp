// The core is built without exceptions. Ubuntu's wasm libc++ still references the throwing
// machinery from operator new, so supply non-throwing allocation and trap if anything ever throws.
#include <cstdlib>
#include <new>

void* operator new(std::size_t n) { void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

extern "C" {
void* __cxa_allocate_exception(std::size_t) { std::abort(); }
void __cxa_throw(void*, void*, void (*)(void*)) { std::abort(); }
}
