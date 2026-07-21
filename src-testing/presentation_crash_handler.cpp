#if defined(__linux__)
#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <unistd.h>

namespace
{
    void print_backtrace(int signal_number)
    {
        const char header[] = "\nPresentation smoke test crashed; native backtrace:\n";
        (void)!write(STDERR_FILENO, header, sizeof(header) - 1);
        void *frames[96];
        const int count = backtrace(frames, (int)(sizeof(frames) / sizeof(frames[0])));
        backtrace_symbols_fd(frames, count, STDERR_FILENO);
        _exit(128 + signal_number);
    }

    struct CrashHandlerInstaller
    {
        CrashHandlerInstaller()
        {
            std::signal(SIGSEGV, print_backtrace);
            std::signal(SIGABRT, print_backtrace);
            std::signal(SIGBUS, print_backtrace);
            std::signal(SIGFPE, print_backtrace);
        }
    } installer;
}
#endif
