#include "logger.h"

namespace
{
    struct TestRuntimeInitializer
    {
        TestRuntimeInitializer()
        {
            if (!logger)
                initLogger();
        }
    };

    TestRuntimeInitializer test_runtime_initializer;
}
