cmake_minimum_required(VERSION 3.22)

add_library(tm1637 STATIC
  Src/tm1637.c)

target_include_directories(tm1637 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc)

target_link_libraries(tm1637 PUBLIC stm32cubemx)