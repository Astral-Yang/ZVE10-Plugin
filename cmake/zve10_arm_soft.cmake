# Sony ZV-E10 (BIONZ X / ARMv7-A soft-float EABI5) 交叉编译工具链文件
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabi-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabi-g++)
set(CMAKE_ASM_COMPILER arm-linux-gnueabi-gcc)

# 强制硬匹配固件 ABI: ARMv7-A + Thumb-2 + soft-float (e_flags = 0x5000002)
set(ZVP_ARM_FLAGS "-march=armv7-a -mthumb -mfloat-abi=soft -fPIC")
set(CMAKE_C_FLAGS_INIT "${ZVP_ARM_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ZVP_ARM_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${ZVP_ARM_FLAGS}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
