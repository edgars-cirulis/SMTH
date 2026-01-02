#include "App.hpp"

#include <cstdio>
#include <exception>

#if defined(_WIN32)
#include <windows.h>
#endif

int main()
{
    try {
        App app;
        app.run();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
#if defined(_WIN32)
        MessageBoxA(nullptr, e.what(), "Fatal error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    } catch (...) {
        std::fprintf(stderr, "Fatal error: unknown exception\n");
#if defined(_WIN32)
        MessageBoxA(nullptr, "Unknown exception", "Fatal error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
