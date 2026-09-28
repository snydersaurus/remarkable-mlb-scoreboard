cmake_minimum_required(VERSION 3.21)
project(mlb_scoreboard VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Core and Network only. The user interface is QML loaded into xochitl by
# AppLoad, so nothing here draws anything -- this is a headless console process.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network)

add_executable(scoreboard_backend
    src/backend_main.cpp
    src/AppLoadLink.cpp
    src/AppLoadLink.h
    src/GameFeed.cpp
    src/GameFeed.h
    src/WakeTimer.h
    src/LogoStore.cpp
    src/LogoStore.h
)

target_link_libraries(scoreboard_backend PRIVATE Qt6::Core Qt6::Network)
