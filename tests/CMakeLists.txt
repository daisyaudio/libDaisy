cmake_minimum_required(VERSION 3.16)
project(daisy-test)

find_package(Threads REQUIRED)
add_subdirectory(googletest)
include(GoogleTest)

set(MODULE_DIR ../src)

# if we're not cross-compiling, we can do unit tests
add_library(daisy STATIC
  ${MODULE_DIR}/hid/midi_parser.cpp
  ${MODULE_DIR}/per/qspi.cpp
  ${MODULE_DIR}/sys/system.cpp
  ${MODULE_DIR}/ui/AbstractMenu.cpp
  ${MODULE_DIR}/ui/UI.cpp
  ${MODULE_DIR}/util/MappedValue.cpp
  ${MODULE_DIR}/util/oled_fonts.c
)
target_include_directories(daisy PUBLIC ${MODULE_DIR})
target_include_directories(daisy PUBLIC
  ${MODULE_DIR}/sys
  ../Middlewares/Third_Party/FatFs/src
)
target_compile_definitions(daisy PUBLIC UNIT_TEST)

# needed because some internal libDaisy testing stuff includes gtest
target_link_libraries(daisy PUBLIC GTest::gtest_main)

enable_testing()

# Add a gtest
function(add_gtest source_file)
  cmake_path(GET source_file STEM test_name)

  # Create the target
  if(test_name STREQUAL "UsbMidiTransmit_gtest")
    # This driver test needs only a fake host controller, not the board library.
    add_executable(${test_name} ${source_file})
  else()
    add_executable(${test_name}
      ${source_file}
      ${CMAKE_CURRENT_LIST_DIR}/FatFsMock.cpp
    )
    target_link_libraries(${test_name} PUBLIC daisy)
  endif()

  # Link it against the required libraries
  target_link_libraries(${test_name} PUBLIC
    Threads::Threads
    GTest::gtest_main
  )

  # Set any compile definitions
  target_compile_definitions(${test_name} PRIVATE
    UNIT_TEST
  )

  # Set the properties
  set_target_properties(${test_name} PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  # Add the target as a test
  #add_test(NAME ${test_name} COMMAND ${test_name} --verbose)

  # Discover unit tests inside it
  gtest_discover_tests(${test_name})
endfunction(add_gtest)

# Generate test executables from files ending in *_gtest.cpp
function(autogen_gtests test_folder)
  file(GLOB_RECURSE specs ${test_folder}/*_gtest.cpp)

  foreach(spec IN LISTS specs)
    add_gtest(${spec})
  endforeach()
endfunction()

autogen_gtests(${CMAKE_CURRENT_LIST_DIR} daisy)

# Compile the real USB MIDI class against host-controller stubs.
set(USB_MIDI_DIR ../Middlewares/ST/STM32_USB_Host_Library/Class/MIDI)
target_sources(UsbMidiTransmit_gtest PRIVATE ${USB_MIDI_DIR}/Src/usbh_midi.c)
target_include_directories(UsbMidiTransmit_gtest BEFORE PRIVATE
  usb_midi_stubs
  ${USB_MIDI_DIR}/Inc
)
