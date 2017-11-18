cmake_minimum_required(VERSION 2.8)

include_directories(/opt/vc/include)
link_directories(/opt/vc/lib)

file(GLOB sourceFiles *.cpp)

add_executable(fbcp-ili9341 ${sourceFiles})

target_link_libraries(fbcp-ili9341 pthread bcm_host)
