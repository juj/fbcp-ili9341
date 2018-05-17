cmake_minimum_required(VERSION 2.8)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

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
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DPI_ZERO=1")
elseif ()
  message(STATUS "Enabling optimization flags that target Raspberry Pi 3 Model B hardware. Pass -DPI_ZERO=ON to target Raspberry Pi Zero (W) instead.")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -marm -mabi=aapcs-linux -march=armv8-a+crc -mcpu=cortex-a53 -mtune=cortex-a53 -mfpu=neon-fp-armv8 -mhard-float -mfloat-abi=hard -mlittle-endian -mtls-dialect=gnu2 -funsafe-math-optimizations")
endif()

option(ADAFRUIT_ILI9341_PITFT "Target Adafruit's ILI9341-based PiTFT display" OFF)
option(FREEPLAYTECH_WAVESHARE32B "Target WaveShare32B ILI9341 display on Freeplaytech's CM3/Zero devices)" OFF)
option(WAVESHARE35B_ILI9486 "Target Waveshare's ILI9486-based Waveshare Wavepear 3.5 inch (B) display" OFF)

option(GPIO_TFT_DATA_CONTROL "Explicitly specify the Data/Control GPIO pin (sometimes also called Register Select)" 0)
option(GPIO_TFT_RESET_PIN "Explicitly specify the Reset GPIO pin (leave out if there is no Reset line)" 0)

option(KERNEL_MODULE_CLIENT "If enabled, run fbcp-ili9341 userland program against the kernel module found in kernel/ subdirectory (must be started before the userland program)" OFF)

option(USE_DMA_TRANSFERS "If enabled, fbcp-ili9341 utilizes DMA to transfer data to the display. Otherwise, Polled SPI mode is used to drive communication with the SPI display" ON)

option(SPI_BUS_CLOCK_DIVISOR "Specify the SPI0 CDIV register value that defines the SPI0 bus speed" 0)

if (GPIO_TFT_DATA_CONTROL)
	message(STATUS "Using GPIO pin ${GPIO_TFT_DATA_CONTROL} for Data/Control line")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGPIO_TFT_DATA_CONTROL=${GPIO_TFT_DATA_CONTROL}")
endif()

if (GPIO_TFT_RESET_PIN)
	message(STATUS "Using GPIO pin ${GPIO_TFT_RESET_PIN} for Reset line")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGPIO_TFT_RESET_PIN=${GPIO_TFT_RESET_PIN}")
endif()

if (SPI_BUS_CLOCK_DIVISOR)
	message(STATUS "SPI_BUS_CLOCK_DIVISOR set to ${SPI_BUS_CLOCK_DIVISOR}. Try setting this to a higher value (must be an even number) if this causes problems. Display update speed = core_freq/divisor. (on Pi3B, by default core_freq=400)")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSPI_BUS_CLOCK_DIVISOR=${SPI_BUS_CLOCK_DIVISOR}")
endif()

if (KERNEL_MODULE_CLIENT)
	message(STATUS "KERNEL_MODULE_CLIENT enabled, building userland program to operate against fbcp-ili9341 kernel module")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DKERNEL_MODULE_CLIENT=1")
endif()

if (USE_DMA_TRANSFERS)
	message(STATUS "USE_DMA_TRANSFERS enabled, this improves performance. Try running CMake with -DUSE_DMA_TRANSFERS=OFF it this causes problems.")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_DMA_TRANSFERS=1")
else()
	message(STATUS "USE_DMA_TRANSFERS is disabled, this is not the most efficient mode. Try running CMake with -DUSE_DMA_TRANSFERS=ON to improve performance.")
endif()

if (ADAFRUIT_ILI9341_PITFT)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DILI9341 -DADAFRUIT_ILI9341_PITFT")
elseif(FREEPLAYTECH_WAVESHARE32B)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DILI9341 -DFREEPLAYTECH_WAVESHARE32B")
elseif(ILI9341)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DILI9341")
elseif(WAVESHARE35B_ILI9486)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DILI9486 -DWAVESHARE35B_ILI9486")
elseif(ILI9486)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DILI9486")
else()
	message(FATAL_ERROR "Please specify either -DADAFRUIT_ILI9341_PITFT=ON or -DFREEPLAYTECH_WAVESHARE32B=ON on command line to CMake! (or contribute ports to more displays yourself)")
endif()

add_executable(fbcp-ili9341 ${sourceFiles})

target_link_libraries(fbcp-ili9341 pthread bcm_host)
