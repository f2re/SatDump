cmake_minimum_required(VERSION 3.12)
project(geonetcast_support)

file(GLOB_RECURSE geonetcast_support_CPPS *.cpp)
add_library(geonetcast_support SHARED ${geonetcast_support_CPPS})
target_link_libraries(geonetcast_support PUBLIC satdump_core)
target_include_directories(geonetcast_support PUBLIC src .)

# Debian/Astra multi-implementation packages expose the serial build under
# hdf5_serial names, while other distributions use the generic hdf5 names.
find_library(LIBHDF5_LIBRARY NAMES hdf5 hdf5_serial)
find_library(LIBHDF5HL_LIBRARY NAMES hdf5_hl hdf5_serial_hl)
find_path(LIBHDF5_INCLUDE_DIR NAMES hdf5.h PATH_SUFFIXES hdf5/serial)

if(LIBHDF5_LIBRARY AND LIBHDF5HL_LIBRARY AND LIBHDF5_INCLUDE_DIR)
  target_compile_definitions(geonetcast_support PUBLIC ENABLE_HDF5_PARSING=1)
  target_include_directories(geonetcast_support PUBLIC ${LIBHDF5_INCLUDE_DIR})
  target_link_libraries(geonetcast_support PUBLIC ${LIBHDF5_LIBRARY} ${LIBHDF5HL_LIBRARY})
  message("Found HDF5 libraries. Geonetcast HDF5 processing enabled!")
else()
  message("Could not find HDF5 libraries. Geonetcast HDF5 processing disabled!")
endif()

if(MSVC OR BUILD_MSVC)
  install(
    TARGETS geonetcast_support
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/satdump/plugins
    RUNTIME_DEPENDENCY_SET satdump_deps)
else()
  install(TARGETS geonetcast_support DESTINATION ${CMAKE_INSTALL_LIBDIR}/satdump/plugins)
endif()
