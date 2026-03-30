cmake_minimum_required(VERSION 3.20)
project(Orbit VERSION 0.2.0 LANGUAGES CXX)

# C++ Standard 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Flags 
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
  -Wall -Wextra -Wpedantic
  $<$<CONFIG:Release>:-O3>
  $<$<CONFIG:Release>:-march=native>
  $<$<CONFIG:Release>:-DNDEBUG>
  $<$<CONFIG:Debug>:-g>
  $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)

# Dependencies 
find_package(Boost 1.90 REQUIRED COMPONENTS system thread)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# nlohmann/json via FetchContent (header-only)
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Sources 
set(ORBIT_SOURCES
  src/core/PriceTable.cpp
  src/core/ArbitrageEngine.cpp
  src/exchanges/BinanceWS.cpp
  src/exchanges/KrakenWS.cpp
  src/exchanges/CoinbaseWS.cpp
  src/exchanges/HyperLiquidWS.cpp
)

# Target 
add_executable(orbit main.cpp ${ORBIT_SOURCES})

target_include_directories(orbit PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${Boost_INCLUDE_DIRS}
)

target_link_libraries(orbit PRIVATE
  Boost::system
  Boost::thread
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
  nlohmann_json::nlohmann_json
)

# Install 
install(TARGETS orbit RUNTIME DESTINATION bin)