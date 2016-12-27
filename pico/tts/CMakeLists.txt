project(pico_test)
cmake_minimum_required(VERSION 2.8)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra")
aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/../lib)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/../compat/include)
LINK_DIRECTORIES(${PROJECT_SOURCE_DIR}/../lib)
TARGET_LINK_LIBRARIES(${PROJECT_NAME} svoxpico)


