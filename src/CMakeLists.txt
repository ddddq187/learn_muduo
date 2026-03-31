set(SOURCES
    CurrentThread.cc
    Thread.cc 
    Timestamp.cc 
)

add_library(muduo STATIC ${SOURCES})

target_include_directories(muduo
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
