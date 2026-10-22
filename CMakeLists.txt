cmake_minimum_required(VERSION 3.16)
project(attackshark-k86 VERSION 0.2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(K86_BUILD_GUI "Build the GTK 3 GUI (k86-gui) if GTK 3 is available" ON)
option(K86_BUILD_TESTS "Build unit tests" ON)
option(K86_BUILD_LIBRARY "Build libk86 (C API for other programs, include/k86/k86.h)" ON)

include(GNUInstallDirs)

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
# PIC and hidden symbols so the core can be linked into libk86 without leaking its C++ symbols.
set_target_properties(k86core PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden
                                         VISIBILITY_INLINES_HIDDEN ON)

if(K86_BUILD_LIBRARY)
  # libk86.so.0: only the k86_* C functions are exported; the core is linked in privately.
  add_library(k86 SHARED src/lib/k86.cpp)
  target_include_directories(k86 PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(k86 PRIVATE k86core)
  target_compile_definitions(k86 PRIVATE K86_VERSION_STRING="${PROJECT_VERSION}")
  target_compile_options(k86 PRIVATE -Wall -Wextra)
  target_link_options(k86 PRIVATE -Wl,--no-undefined -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/lib/k86.map)
  set_target_properties(k86 PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/k86.map)
  set_target_properties(k86 PROPERTIES VERSION 0 SOVERSION 0 CXX_VISIBILITY_PRESET hidden
                                       VISIBILITY_INLINES_HIDDEN ON)
  file(RELATIVE_PATH K86_PC_RELPREFIX "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig" "${CMAKE_INSTALL_PREFIX}")
  string(REGEX REPLACE "/$" "" K86_PC_RELPREFIX "${K86_PC_RELPREFIX}")
  configure_file(packaging/k86.pc.in k86.pc @ONLY)
  install(TARGETS k86 LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(FILES include/k86/k86.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/k86)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/k86.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

add_executable(k86ctl src/cli/main.cpp)
target_link_libraries(k86ctl PRIVATE k86core)
target_compile_options(k86ctl PRIVATE -Wall -Wextra)

if(K86_BUILD_GUI)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(AYATANA QUIET IMPORTED_TARGET ayatana-appindicator3-0.1)
  endif()
  if(GTK3_FOUND)
    add_executable(k86-gui src/gui/main.cpp src/gui/worker.cpp src/gui/keyboard_view.cpp src/gui/widgets.cpp
                           src/gui/page_keys.cpp src/gui/page_lighting.cpp src/gui/page_macros.cpp
                           src/gui/page_device.cpp src/gui/selftest.cpp src/gui/tray.cpp
                           src/gui/app_settings.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(k86-gui PRIVATE k86core PkgConfig::GTK3 Threads::Threads)
    target_compile_options(k86-gui PRIVATE -Wall -Wextra)
    # Tray icon for builds that are run without installing (installed builds use the icon theme).
    target_compile_definitions(k86-gui PRIVATE K86_SOURCE_ICON_DIR="${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons")
    install(TARGETS k86-gui RUNTIME DESTINATION bin)
    install(FILES packaging/k86-gui.desktop DESTINATION share/applications)
    install(DIRECTORY packaging/icons/ DESTINATION share/icons)
    if(AYATANA_FOUND)
      target_link_libraries(k86-gui PRIVATE PkgConfig::AYATANA)
      target_compile_definitions(k86-gui PRIVATE K86_HAVE_AYATANA)
      message(STATUS "libayatana-appindicator found: k86-gui gets a tray icon")
    else()
      message(STATUS "libayatana-appindicator not found: k86-gui is built without a tray icon")
    endif()
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
  if(TARGET k86)
    # Uses only the public header, compiled as C.
    add_executable(k86capitest tests/test_capi.c)
    set_target_properties(k86capitest PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
    target_compile_options(k86capitest PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(k86capitest PRIVATE k86)
    add_test(NAME k86capitest COMMAND k86capitest)
  endif()
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
