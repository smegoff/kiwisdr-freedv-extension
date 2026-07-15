cmake_minimum_required(VERSION 3.16)
project(kiwi_freedv_decoder VERSION 0.1.19 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost 1.74 REQUIRED COMPONENTS system)
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CODEC2 codec2)
pkg_check_modules(SAMPLERATE REQUIRED samplerate)

find_path(RADE_INCLUDE_DIR rade_api.h PATH_SUFFIXES freedv-rade/rade)
find_path(FARGAN_INCLUDE_DIR fargan.h PATH_SUFFIXES freedv-rade/opus/dnn)
find_path(OPUS_INCLUDE_DIR opus_types.h PATH_SUFFIXES freedv-rade/opus/include)
find_library(RADE_LIBRARY NAMES rade)
find_library(FARGAN_LIBRARY NAMES opus-fargan
  PATHS /usr/local/lib/freedv-rade
  NO_DEFAULT_PATH)

set(HAVE_RADE FALSE)
if(RADE_INCLUDE_DIR AND FARGAN_INCLUDE_DIR AND OPUS_INCLUDE_DIR AND RADE_LIBRARY AND FARGAN_LIBRARY)
  set(HAVE_RADE TRUE)
  get_filename_component(FARGAN_ROOT_DIR "${FARGAN_INCLUDE_DIR}" DIRECTORY)
  message(STATUS "RADEv1 backend enabled using ${RADE_LIBRARY}")
else()
  message(STATUS "RADEv1 backend unavailable; install the pinned portable C library with deploy/build-radae.sh")
endif()

function(enable_radev1 target)
  if(HAVE_RADE)
    target_sources(${target} PRIVATE src/rade_v1_wrapper.c)
    target_include_directories(${target} PRIVATE
      ${RADE_INCLUDE_DIR}
      ${FARGAN_ROOT_DIR}
      ${FARGAN_ROOT_DIR}/dnn
      ${FARGAN_ROOT_DIR}/celt
      ${FARGAN_ROOT_DIR}/include)
    target_link_libraries(${target} PRIVATE ${RADE_LIBRARY} ${FARGAN_LIBRARY} m)
    target_compile_definitions(${target} PRIVATE HAVE_RADE=1)
  endif()
endfunction()

add_executable(freedv-decoder
  src/main.cpp
  src/kiwi_protocol.cpp
  src/resampler.cpp
  src/codec2_backend.cpp
  src/rade_backend.cpp
)
target_include_directories(freedv-decoder PRIVATE include ${CODEC2_INCLUDE_DIRS} ${SAMPLERATE_INCLUDE_DIRS})
target_link_libraries(freedv-decoder PRIVATE Boost::system Threads::Threads OpenSSL::Crypto nlohmann_json::nlohmann_json ${CODEC2_LIBRARIES} ${SAMPLERATE_LIBRARIES})
target_compile_options(freedv-decoder PRIVATE -Wall -Wextra -Wpedantic ${CODEC2_CFLAGS_OTHER} ${SAMPLERATE_CFLAGS_OTHER})
if(CODEC2_FOUND)
  target_compile_definitions(freedv-decoder PRIVATE HAVE_CODEC2=1)
endif()
enable_radev1(freedv-decoder)

include(CTest)
if(BUILD_TESTING)
  add_executable(freedv-protocol-test
    tests/protocol_test.cpp
    src/kiwi_protocol.cpp
  )
  target_include_directories(freedv-protocol-test PRIVATE include)
  target_link_libraries(freedv-protocol-test PRIVATE OpenSSL::Crypto nlohmann_json::nlohmann_json)
  # Release builds define NDEBUG globally. Unit-test assertions must remain live.
  target_compile_options(freedv-protocol-test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME freedv-protocol-test COMMAND freedv-protocol-test)

  add_executable(freedv-backend-test
    tests/backend_test.cpp
    src/codec2_backend.cpp
    src/rade_backend.cpp
  )
  target_include_directories(freedv-backend-test PRIVATE include ${CODEC2_INCLUDE_DIRS})
  target_link_libraries(freedv-backend-test PRIVATE ${CODEC2_LIBRARIES})
  target_compile_options(freedv-backend-test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG ${CODEC2_CFLAGS_OTHER})
  if(CODEC2_FOUND)
    target_compile_definitions(freedv-backend-test PRIVATE HAVE_CODEC2=1)
  endif()
  enable_radev1(freedv-backend-test)
  add_test(NAME freedv-backend-test COMMAND freedv-backend-test)

  if(HAVE_RADE)
    add_executable(freedv-rade-reference-test
      tests/rade_reference_test.cpp
      src/rade_backend.cpp
    )
    target_include_directories(freedv-rade-reference-test PRIVATE include)
    target_compile_options(freedv-rade-reference-test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    enable_radev1(freedv-rade-reference-test)
  endif()

  add_executable(freedv-reference-test
    tests/reference_test.cpp
    src/codec2_backend.cpp
  )
  target_include_directories(freedv-reference-test PRIVATE include ${CODEC2_INCLUDE_DIRS} ${SAMPLERATE_INCLUDE_DIRS})
  target_link_libraries(freedv-reference-test PRIVATE ${CODEC2_LIBRARIES} ${SAMPLERATE_LIBRARIES})
  target_compile_options(freedv-reference-test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG ${CODEC2_CFLAGS_OTHER} ${SAMPLERATE_CFLAGS_OTHER})
  if(CODEC2_FOUND)
    target_compile_definitions(freedv-reference-test PRIVATE HAVE_CODEC2=1)
  endif()

  add_executable(freedv-transport-reference-test
    tests/transport_reference_test.cpp
    src/kiwi_protocol.cpp
    src/resampler.cpp
    src/codec2_backend.cpp
  )
  target_include_directories(freedv-transport-reference-test PRIVATE include ${CODEC2_INCLUDE_DIRS} ${SAMPLERATE_INCLUDE_DIRS})
  target_link_libraries(freedv-transport-reference-test PRIVATE OpenSSL::Crypto nlohmann_json::nlohmann_json ${CODEC2_LIBRARIES} ${SAMPLERATE_LIBRARIES})
  target_compile_options(freedv-transport-reference-test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG ${CODEC2_CFLAGS_OTHER} ${SAMPLERATE_CFLAGS_OTHER})
  if(CODEC2_FOUND)
    target_compile_definitions(freedv-transport-reference-test PRIVATE HAVE_CODEC2=1)
  endif()
endif()

install(TARGETS freedv-decoder RUNTIME DESTINATION bin)
