cmake_minimum_required(VERSION 2.8)

include_directories(/opt/vc/include)
link_directories(/opt/vc/lib)

file(GLOB sourceFiles *.cpp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -marm -mabi=aapcs-linux -march=armv8-a+crc -mcpu=cortex-a53 -mtune=cortex-a53 -mfpu=neon-fp-armv8 -mhard-float -mfloat-abi=hard -mlittle-endian -mtls-dialect=gnu2 -funsafe-math-optimizations")

add_executable(fbcp-ili9341 ${sourceFiles})

target_link_libraries(fbcp-ili9341 pthread bcm_host)
