cmake_minimum_required(VERSION 3.15)
project(lockfree)

set(CMAKE_CXX_STANDARD 17)

# required for clang completion in editors - must be set after creating project
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_executable(lockfree
  lockfree.cpp
)
target_link_libraries(lockfree
  ${CMAKE_THREAD_LIBS_INIT}
)
target_include_directories(lockfree
    PUBLIC
    ${CMAKE_SOURCE_DIR}/atomic_queue/include
)

add_executable(lockfreelatency
  lockfreelatency.cpp
)
target_link_libraries(lockfreelatency
  ${CMAKE_THREAD_LIBS_INIT}
)
target_include_directories(lockfreelatency
    PUBLIC
    ${CMAKE_SOURCE_DIR}/atomic_queue/include
)

add_executable(condvarlatency
  condvarlatency.cpp
)
target_link_libraries(condvarlatency
  ${CMAKE_THREAD_LIBS_INIT}
)
target_include_directories(condvarlatency
    PUBLIC
    ${CMAKE_SOURCE_DIR}/atomic_queue/include
)
