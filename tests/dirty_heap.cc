// Link into a test to make every C++ allocation start as 0xFF bytes (a NaN as float), like reused heap
// memory in a long-running host -- the Eurorack module's RAM starts zeroed, a plugin's doesn't.
#include <cstdlib>
#include <cstring>
#include <new>
void *operator new(std::size_t n) { void *p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); std::memset(p, 0xFF, n); return p; }
void *operator new[](std::size_t n) { return operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
