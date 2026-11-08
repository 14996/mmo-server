cmake_minimum_required(VERSION 3.16)
project(mmo_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(mmo_core STATIC
  src/Acceptor.cpp
  src/Buffer.cpp
  src/Channel.cpp
  src/Connection.cpp
  src/EventLoop.cpp
  src/GameConfig.cpp
  src/GameServer.cpp
  src/Messages.cpp
  src/Poller.cpp
  src/Protocol.cpp
  src/Server.cpp
  src/ServerConfig.cpp
  src/Socket.cpp
  src/ThreadPool.cpp
  src/World.cpp
  src/util.cpp
)
target_include_directories(mmo_core PUBLIC src/include)
target_link_libraries(mmo_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(mmo_core PUBLIC ws2_32)
endif()

add_executable(mmo_server src/server_main.cpp)
target_link_libraries(mmo_server PRIVATE mmo_core)
if(MINGW)
  target_link_options(mmo_server PRIVATE -static)
endif()

add_executable(bot_client
  tools/bot_client.cpp
  src/Messages.cpp
  src/Protocol.cpp
  src/util.cpp
)
target_include_directories(bot_client PRIVATE src/include)
target_link_libraries(bot_client PRIVATE Threads::Threads)
if(WIN32)
  target_link_libraries(bot_client PRIVATE ws2_32)
endif()
if(MINGW)
  target_link_options(bot_client PRIVATE -static)
endif()

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE mmo_core)
if(MINGW)
  target_link_options(test_core PRIVATE -static)
endif()
add_test(NAME core_tests COMMAND test_core)
