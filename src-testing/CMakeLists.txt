cmake_minimum_required(VERSION 3.12)

file(GLOB_RECURSE SatDump_testing_CPPS main.cpp)

add_executable(satdump-testing ${SatDump_testing_CPPS})
target_include_directories(satdump-testing PUBLIC src-testing src-core)

# Link against the core
target_link_libraries(satdump-testing PUBLIC satdump_core) # mfhdf)

add_executable(satdump-presentation-test presentation_test.cpp)
target_include_directories(satdump-presentation-test PUBLIC src-testing src-core)
target_link_libraries(satdump-presentation-test PUBLIC satdump_core)

add_executable(satdump-map-label-test map_label_test.cpp)
target_include_directories(satdump-map-label-test PUBLIC src-testing src-core)
target_link_libraries(satdump-map-label-test PUBLIC satdump_core)

add_executable(satdump-presentation-semantics-test presentation_semantics_test.cpp)
target_include_directories(satdump-presentation-semantics-test PUBLIC src-testing src-core)
target_link_libraries(satdump-presentation-semantics-test PUBLIC satdump_core)
