cmake_minimum_required(VERSION 3.16)
project(emotion_keychain CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Движок лиц: без исключений, RTTI, кучи и libm — так же он собирается для МК и WebAssembly.
file(GLOB FACE_ENGINE_SOURCES CONFIGURE_DEPENDS engine/src/*.cpp)
add_library(face_engine STATIC ${FACE_ENGINE_SOURCES})
target_include_directories(face_engine PUBLIC engine/include)
target_compile_options(face_engine PRIVATE -Wall -Wextra -Wshadow -fno-exceptions -fno-rtti -fno-math-errno)

add_library(face_sim_common STATIC sim/common/gif_writer.cpp sim/common/oled_look.cpp)
target_include_directories(face_sim_common PUBLIC sim/common)
target_link_libraries(face_sim_common PUBLIC face_engine)

add_executable(facegen sim/cli/facegen.cpp)
target_link_libraries(facegen PRIVATE face_sim_common)

enable_testing()
add_executable(face_tests engine/tests/tests.cpp)
target_link_libraries(face_tests PRIVATE face_engine)
add_test(NAME face_tests COMMAND face_tests)

# Прошивка стенда на ПК: заглушки Arduino и эмулятор контроллера экрана вместо железа.
add_executable(firmware_tests firmware/host/stand_test.cpp firmware/src/ssd1306.cpp)
target_include_directories(firmware_tests PRIVATE firmware/host firmware/src)
target_compile_options(firmware_tests PRIVATE -Wall -Wextra)
target_compile_definitions(firmware_tests PRIVATE PIN_BUZZER=5)
target_link_libraries(firmware_tests PRIVATE face_engine)
add_test(NAME firmware_tests COMMAND firmware_tests)
add_executable(storage_tests firmware/host/storage_test.cpp firmware/src/keychain/storage.cpp)
target_include_directories(storage_tests PRIVATE firmware/host firmware/src/keychain)
target_compile_options(storage_tests PRIVATE -Wall -Wextra)
target_compile_definitions(storage_tests PRIVATE STORAGE_HOST_TEST)
add_test(NAME storage_tests COMMAND storage_tests)

# Оконный симулятор, если найден SDL2.
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_executable(facesim sim/sdl/facesim.cpp)
  target_link_libraries(facesim PRIVATE face_sim_common ${SDL2_LIBRARIES})
  target_include_directories(facesim PRIVATE ${SDL2_INCLUDE_DIRS})
  message(STATUS "SDL2 найден: собираю facesim")
else()
  message(STATUS "SDL2 не найден: facesim не собирается (веб-симулятор работает без него)")
endif()
