cmake_minimum_required(VERSION 3.12)

file(GLOB_RECURSE SatDump_testing_CPPS main.cpp)

add_executable(satdump-testing ${SatDump_testing_CPPS})
target_include_directories(satdump-testing PUBLIC src-testing src-core)

# Link against the core
target_link_libraries(satdump-testing PUBLIC satdump_core) # mfhdf)

add_executable(satdump-presentation-test
    presentation_test.cpp
    presentation_crash_handler.cpp
)
target_include_directories(satdump-presentation-test PUBLIC src-testing src-core)
target_link_libraries(satdump-presentation-test PUBLIC satdump_core)

if(UNIX AND NOT APPLE)
    # Keep function names visible in the crash backtrace printed by the smoke test.
    target_link_options(satdump-presentation-test PRIVATE -rdynamic)
endif()
