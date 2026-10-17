cmake_minimum_required(VERSION 3.16)
project(attackshark-k86 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(K86_BUILD_GUI "Build the Qt6 GUI (k86-gui) if Qt6 is available" ON)
option(K86_BUILD_TESTS "Build unit tests" ON)

add_library(k86core STATIC
  src/core/hidraw.cpp
  src/core/protocol.cpp
  src/core/keycodes.cpp
  src/core/macro.cpp
  src/core/keyboard.cpp
  src/core/screen.cpp
  src/core/sysinfo.cpp
  src/core/discovery.cpp
)
target_include_directories(k86core PUBLIC src/core)
target_include_directories(k86core SYSTEM PRIVATE third_party/stb)
target_compile_options(k86core PRIVATE -Wall -Wextra)

add_executable(k86ctl src/cli/main.cpp)
target_link_libraries(k86ctl PRIVATE k86core)
target_compile_options(k86ctl PRIVATE -Wall -Wextra)

if(K86_BUILD_GUI)
  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_executable(k86-gui src/gui/main.cpp src/gui/mainwindow.cpp src/gui/mainwindow.hpp)
    target_link_libraries(k86-gui PRIVATE k86core Qt6::Widgets)
    install(TARGETS k86-gui RUNTIME DESTINATION bin)
    message(STATUS "Qt6 found: building k86-gui")
  else()
    message(STATUS "Qt6 not found: skipping k86-gui")
  endif()
endif()

if(K86_BUILD_TESTS)
  enable_testing()
  add_executable(k86tests tests/test_main.cpp)
  target_link_libraries(k86tests PRIVATE k86core)
  target_compile_definitions(k86tests PRIVATE K86_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
  add_test(NAME k86tests COMMAND k86tests)
endif()

install(TARGETS k86ctl RUNTIME DESTINATION bin)
install(FILES udev/70-attackshark-k86.rules DESTINATION lib/udev/rules.d)
