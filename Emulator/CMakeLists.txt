cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# Set the project name
project(vAmigaCore)

# Enable testing
list(APPEND CMAKE_CTEST_ARGUMENTS "--verbose")
enable_testing()

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS False)

# Set the optimization level (if not specified on the command line)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Find and optionally link zlib
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
  message(STATUS "Found ZLIB: ${ZLIB_LIBRARIES}")
else()
  message(STATUS "ZLIB not found. Compression support will be disabled.")
endif()

# Add the emulator library
add_library(vAmigaCore VAmiga.cpp VAmigaConfig.cpp)

# Add the self-test app (vAmiga vAmigaCheck)
add_executable(vAmigaCheck VAmigaCheck.cpp VAmigaConfig.cpp)
target_link_libraries(vAmigaCheck vAmigaCore)

# Specify compile options
target_compile_definitions(vAmigaCore PUBLIC _USE_MATH_DEFINES)
if(WIN32)
  target_link_libraries(vAmigaCheck ws2_32)
endif()
if(MSVC)
  target_compile_options(vAmigaCore PUBLIC /W4 /bigobj /Zc:preprocessor) # /WX disabled for now
  target_compile_options(vAmigaCore PUBLIC /wd4100 /wd4201 /wd4324 /wd4458 /wd4091 /wd4430)
  set_source_files_properties(CPU/Moira/Moira.cpp PROPERTIES COMPILE_FLAGS "/wd4127")
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(vAmigaCore PUBLIC -Wall -Werror)
  target_compile_options(vAmigaCore PUBLIC -Wno-restrict)
  target_compile_options(vAmigaCore PUBLIC -Wno-unused-parameter)
# target_compile_options(vAmigaCore PUBLIC -Wno-array-bounds)
  target_compile_options(vAmigaCore PUBLIC -fconcepts)
else()
  target_compile_options(vAmigaCore PUBLIC -Wall -Werror)
  target_compile_options(vAmigaCore PUBLIC -Wno-unused-parameter)
  target_compile_options(vAmigaCore PUBLIC -Wno-gnu-anonymous-struct)
  target_compile_options(vAmigaCore PUBLIC -Wno-nested-anon-types)
endif()

# Add include paths
target_include_directories(vAmigaCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Add subdirectories
add_subdirectory(VAmiga)

# Add libraries
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(vAmigaCore xdms Threads::Threads)

# Link zlib if available
if(ZLIB_FOUND)
  target_link_libraries(vAmigaCore ZLIB::ZLIB)
  target_link_libraries(vAmigaCheck ZLIB::ZLIB)
  target_compile_definitions(vAmigaCore PUBLIC USE_ZLIB=1)  # Optional define for conditional compilation
endif()

target_link_libraries(vAmigaCore lz4)
target_link_libraries(vAmigaCheck lz4)

# Add tests
add_test(NAME SelfTest1 COMMAND vAmigaCheck --verbose --footprint)
add_test(NAME SelfTest2 COMMAND vAmigaCheck --verbose --smoke)
add_test(NAME SelfTest3 COMMAND vAmigaCheck --verbose --diagnose)
