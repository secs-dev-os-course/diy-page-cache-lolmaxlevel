cmake_minimum_required(VERSION 3.12)
project(app
        VERSION 0.0.0
        DESCRIPTION "Application module"
        LANGUAGES CXX
)

cmake_policy(SET CMP0076 NEW) # avoid warning of relative paths translation

add_executable(app)
target_include_directories(app PRIVATE ${CMAKE_SOURCE_DIR})

target_sources(app
        PRIVATE
        cache.cpp
        cache.h
        app.cpp
)