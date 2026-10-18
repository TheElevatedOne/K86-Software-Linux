cmake_minimum_required(VERSION 3.16)
project(attackshark-k86 VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(K86_BUILD_GUI "Build the GTK 3 GUI (k86-gui) if GTK 3 is available" ON)
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
  src/core/layout.cpp
  src/core/simulator.cpp
)
target_include_directories(k86core PUBLIC src/core)
target_include_directories(k86core SYSTEM PRIVATE third_party/stb)
target_compile_options(k86core PRIVATE -Wall -Wextra)

add_executable(k86ctl src/cli/main.cpp)
target_link_libraries(k86ctl PRIVATE k86core)
target_compile_options(k86ctl PRIVATE -Wall -Wextra)

if(K86_BUILD_GUI)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
  endif()
  if(GTK3_FOUND)
    add_executable(k86-gui src/gui/main.cpp src/gui/worker.cpp src/gui/keyboard_view.cpp src/gui/widgets.cpp
                           src/gui/page_keys.cpp src/gui/page_lighting.cpp src/gui/page_macros.cpp
                           src/gui/page_device.cpp src/gui/selftest.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(k86-gui PRIVATE k86core PkgConfig::GTK3 Threads::Threads)
    target_compile_options(k86-gui PRIVATE -Wall -Wextra)
    install(TARGETS k86-gui RUNTIME DESTINATION bin)
    install(FILES packaging/k86-gui.desktop DESTINATION share/applications)
    install(DIRECTORY packaging/icons/ DESTINATION share/icons)
    message(STATUS "GTK 3 found: building k86-gui")
  else()
    message(STATUS "GTK 3 not found: skipping k86-gui")
  endif()
endif()

if(K86_BUILD_TESTS)
  enable_testing()
  add_executable(k86tests tests/test_main.cpp)
  target_link_libraries(k86tests PRIVATE k86core)
  target_compile_definitions(k86tests PRIVATE K86_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
  add_test(NAME k86tests COMMAND k86tests)
  if(TARGET k86-gui)
    # Clicks through the GUI against a simulated keyboard (skipped without a display).
    foreach(fw v1 v2)
      add_test(NAME k86-gui-selftest-${fw} COMMAND k86-gui --self-test --dry-run=${fw})
      set_tests_properties(k86-gui-selftest-${fw} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    endforeach()
  endif()
endif()

install(TARGETS k86ctl RUNTIME DESTINATION bin)
install(FILES udev/70-attackshark-k86.rules DESTINATION lib/udev/rules.d)
