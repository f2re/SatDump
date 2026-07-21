#include "logger.h"

#if defined(__linux__)
#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <unistd.h>
#endif

namespace
{
#if defined(__linux__)
    void print_backtrace(int signal_number)
    {
        const char header[] = "\nPresentation smoke test crashed; native backtrace:\n";
        (void)!write(STDERR_FILENO, header, sizeof(header) - 1);
        void *frames[96];
        const int count = backtrace(frames, (int)(sizeof(frames) / sizeof(frames[0])));
        backtrace_symbols_fd(frames, count, STDERR_FILENO);
        _exit(128 + signal_number);
    }
#endif

    struct PresentationTestRuntime
    {
        PresentationTestRuntime()
        {
            // image::save_img and several other core helpers log through SatDump's
            // global logger. The standalone test must initialize it exactly as the
            // CLI/UI entry points do before exercising those helpers.
            initLogger();
            completeLoggerInit();

#if defined(__linux__)
            std::signal(SIGSEGV, print_backtrace);
            std::signal(SIGABRT, print_backtrace);
            std::signal(SIGBUS, print_backtrace);
            std::signal(SIGFPE, print_backtrace);
#endif
        }
    } runtime;
}
