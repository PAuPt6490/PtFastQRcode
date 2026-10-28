// Minimal CRT replacement (C++ side): allocator, startup, STL termination hooks.
// Memory and wide-string primitives live in crt_mem.c, compiled as C so they can
// shadow the MSVC intrinsics.
#include <windows.h>
#include <stddef.h>

extern "C" int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int);

// ---------------------------------------------------------------- allocators

static HANDLE processHeap() { return GetProcessHeap(); }

extern "C" void* __cdecl malloc(size_t size) {
    return HeapAlloc(processHeap(), 0, size ? size : 1);
}

extern "C" void __cdecl free(void* block) {
    if (block) HeapFree(processHeap(), 0, block);
}

extern "C" void* __cdecl realloc(void* block, size_t size) {
    if (!block) return malloc(size);
    if (size == 0) {
        free(block);
        return nullptr;
    }
    return HeapReAlloc(processHeap(), 0, block, size);
}

extern "C" void* __cdecl calloc(size_t count, size_t size) {
    const size_t total = count * size;
    return HeapAlloc(processHeap(), HEAP_ZERO_MEMORY, total ? total : 1);
}

void* operator new(size_t size) { return malloc(size); }
void* operator new[](size_t size) { return malloc(size); }
void operator delete(void* block) noexcept { free(block); }
void operator delete[](void* block) noexcept { free(block); }
void operator delete(void* block, size_t) noexcept { free(block); }
void operator delete[](void* block, size_t) noexcept { free(block); }

// ---------------------------------------------------------------- misc hooks

extern "C" int __cdecl atexit(void(__cdecl*)(void)) {
    return 0;  // the OS reclaims static storage at exit; nothing needs a destructor
}

extern "C" void __cdecl abort(void) { ExitProcess(3); }

namespace stdext {
class exception;
}

namespace std {
void (*_Raise_handler)(const stdext::exception&) = nullptr;

__declspec(noreturn) void __cdecl _Xout_of_range(char const*) { ExitProcess(4); }
__declspec(noreturn) void __cdecl _Xlength_error(char const*) { ExitProcess(5); }
}  // namespace std

// ---------------------------------------------------------------- startup

#pragma section(".CRT$XCA", read)
#pragma section(".CRT$XCZ", read)
typedef void(__cdecl* InitFn)();
__declspec(allocate(".CRT$XCA")) static InitFn g_ctorFirst = nullptr;
__declspec(allocate(".CRT$XCZ")) static InitFn g_ctorLast = nullptr;

static void runInitialisers() {
    for (InitFn* fn = &g_ctorFirst; fn < &g_ctorLast; ++fn) {
        if (*fn) (*fn)();
    }
}

// The CRT normally strips the program path from the command line before wWinMain.
static PWSTR commandLineArguments() {
    PWSTR commandLine = GetCommandLineW();
    PWSTR cursor = commandLine;
    if (*cursor == L'"') {
        ++cursor;
        while (*cursor && *cursor != L'"') ++cursor;
        if (*cursor) ++cursor;
    } else {
        while (*cursor && *cursor != L' ' && *cursor != L'\t') ++cursor;
    }
    while (*cursor == L' ' || *cursor == L'\t') ++cursor;
    return cursor;
}

extern "C" __declspec(noreturn) void __cdecl entryPoint() {
    runInitialisers();
    const int result = wWinMain(GetModuleHandleW(nullptr), nullptr, commandLineArguments(),
                                SW_SHOW);
    ExitProcess((UINT)result);
}
