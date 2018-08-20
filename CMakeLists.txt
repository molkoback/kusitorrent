cmake_minimum_required(VERSION 3.9)

project(kusitorrent_project)

set(CMAKE_AUTOMOC ON)

# Sources
set(KUSITORRENT_SRC
	kusitorrent.cpp
)

# Executables
add_executable(kusitorrent ${KUSITORRENT_SRC})

# CXXFlags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -pedantic -Wall -Wextra -pipe")
# Debug
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -Og -ggdb -DDEBUG")
# Release
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3 -march=native -flto")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELEASE} -DDEBUG")
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -march=native -flto")

find_package(Qt5 REQUIRED Core)
find_package(KF5Torrent REQUIRED)
find_package(KF5Crash REQUIRED)
find_package(KF5I18n REQUIRED)
target_link_libraries(kusitorrent Qt5::Core KF5::Torrent)
