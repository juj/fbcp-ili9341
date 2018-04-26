cmake_minimum_required(VERSION 2.8)

include_directories(/opt/vc/include)
link_directories(/opt/vc/lib)

file(GLOB sourceFiles *.cpp)

option(PI_ZERO "Target Raspberry Pi Zero (W)" OFF)

message(STATUS "Doing a ${CMAKE_BUILD_TYPE} build")
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
  message(STATUS "Pass -DCMAKE_BUILD_TYPE=Release to do a fast optimized build.")
endif()

if (PI_ZERO)
  message(STATUS "Targeting Raspberry Pi Zero (W)")
elseif ()
  message(STATUS "Enabling optimization flags that target Raspberry Pi 3 Model B hardware. Pass -DPI_ZERO=ON to target Raspberry Pi Zero (W) instead.")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -marm -mabi=aapcs-linux -march=armv8-a+crc -mcpu=cortex-a53 -mtune=cortex-a53 -mfpu=neon-fp-armv8 -mhard-float -mfloat-abi=hard -mlittle-endian -mtls-dialect=gnu2 -funsafe-math-optimizations")
endif()

option(ADAFRUIT_ILI9341_PITFT "Target Adafruit's ILI9341-based PiTFT display" OFF)
option(FREEPLAYTECH_WAVESHARE32B "Target WaveShare32B ILI9341 display on Freeplaytech's CM3/Zero devices)" OFF)

option(GPIO_TFT_DATA_CONTROL "Explicitly specify the Data/Control GPIO pin (sometimes also called Register Select)" 0)
option(GPIO_TFT_RESET_PIN "Explicitly specify the Reset GPIO pin (leave out if there is no Reset line)" 0)

if (GPIO_TFT_DATA_CONTROL)
	message(STATUS "Using GPIO pin ${GPIO_TFT_DATA_CONTROL} for Data/Control line")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGPIO_TFT_DATA_CONTROL=${GPIO_TFT_DATA_CONTROL}")
endif()

if (GPIO_TFT_RESET_PIN)
	message(STATUS "Using GPIO pin ${GPIO_TFT_RESET_PIN} for Reset line")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGPIO_TFT_RESET_PIN=${GPIO_TFT_RESET_PIN}")
endif()

if (ADAFRUIT_ILI9341_PITFT)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DILI9341 -DADAFRUIT_ILI9341_PITFT")
elseif(FREEPLAYTECH_WAVESHARE32B)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DILI9341 -DFREEPLAYTECH_WAVESHARE32B")
elseif(ILI9341)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DILI9341")
else()
	message(FATAL_ERROR "Please specify either -DADAFRUIT_ILI9341_PITFT=ON or -DFREEPLAYTECH_WAVESHARE32B=ON on command line to CMake! (or contribute ports to more displays yourself)")
endif()

add_executable(fbcp-ili9341 ${sourceFiles})

target_link_libraries(fbcp-ili9341 pthread bcm_host)
