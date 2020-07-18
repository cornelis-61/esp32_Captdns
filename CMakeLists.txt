cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

#run this command to get cquery db built
#cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=YES

#Flashing command
#execute_process(COMMAND idf.py "flash")
#execute_process(COMMAND esptool.py "--port COM8 write_flash 0x10000 build\\Pneumobil.bin")

#Create documentation
#execute_process(COMMAND doxygen Doxyfile)


project(Captdns)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)