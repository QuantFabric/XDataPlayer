cmake_minimum_required(VERSION 3.16)

PROJECT(XDataPlayer)

set(CMAKE_CXX_FLAGS "-fPIC")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

SET(AppVersion 0.3.0)
SET(XDataPlayer_Version ${PROJECT_NAME}_${AppVersion})

# 默认输出debug版
#SET(CMAKE_BUILD_TYPE "Release")
#SET(CMAKE_BUILD_TYPE "debug")
SET(CMAKE_BUILD_TYPE "RelWithDebInfo")
set(CMAKE_CXX_STANDARD 17)

add_definitions(-DFMT_HEADER_ONLY)
add_definitions(-DFMTLOG_HEADER_ONLY)
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Utils/)

# HP-Scoket
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../XAPI/HP-Socket/5.8.2/include/)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../XAPI/HP-Socket/5.8.2/lib)

# FMTLogger
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../XAPI/FMTLogger/include/)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../XAPI/FMTLogger/lib)

# YAML-CPP
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../XAPI/YAML-CPP/0.8.0/include/)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../XAPI/YAML-CPP/0.8.0/lib)

# SHMServer
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../SHMServer/)

set(SOURCES main.cpp 
            HPPackClient.cpp 
            PlayerEngine.cpp)
add_executable(${XDataPlayer_Version} ${SOURCES})

target_link_libraries(${XDataPlayer_Version} pthread hpsocket4c yaml-cpp dl rt)                      
