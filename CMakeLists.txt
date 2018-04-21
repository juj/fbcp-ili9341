cmake_minimum_required(VERSION 2.8)

include_directories(/opt/vc/include)
link_directories(/opt/vc/lib)

file(GLOB sourceFiles *.cpp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -marm -mabi=aapcs-linux -march=armv8-a+crc -mcpu=cortex-a53 -mtune=cortex-a53 -mfpu=neon-fp-armv8 -mhard-float -mfloat-abi=hard -mlittle-endian -mtls-dialect=gnu2 -funsafe-math-optimizations")

option(ADAFRUIT_ILI9341_PITFT "Target Adafruit's ILI9341-based PiTFT display" OFF)
option(FREEPLAYTECH_WAVESHARE32B "Target WaveShare32B ILI9341 display on Freeplaytech's CM3/Zero devices)" OFF)

if (ADAFRUIT_ILI9341_PITFT)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DILI9341 -DADAFRUIT_ILI9341_PITFT")
elseif(FREEPLAYTECH_WAVESHARE32B)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DILI9341 -DFREEPLAYTECH_WAVESHARE32B")
else()
	message(FATAL_ERROR "Please specify either -DADAFRUIT_ILI9341_PITFT=ON or -DFREEPLAYTECH_WAVESHARE32B=ON on command line to CMake! (or contribute ports to more displays yourself)")
endif()

add_executable(fbcp-ili9341 ${sourceFiles})

target_link_libraries(fbcp-ili9341 pthread bcm_host)
