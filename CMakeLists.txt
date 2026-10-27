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

# Плата rev B целиком на ПК: настоящая прошивка firmware/src/keychain, модели микросхем,
# эфир между брелоками, батарея и сценарии (firmware/host/keychain). Флеш журнала
# отображается на тот же адрес 0xEA000, поэтому нужен Linux.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(KC_SAN -fsanitize=address,undefined -fno-omit-frame-pointer)
  add_library(face_engine_san STATIC ${FACE_ENGINE_SOURCES})
  target_include_directories(face_engine_san PUBLIC engine/include)
  target_compile_options(face_engine_san PRIVATE ${KC_SAN} -g -O1)
  add_executable(keychain_board
    firmware/host/keychain/board.cpp firmware/host/keychain/models.cpp
    firmware/src/keychain/hal.cpp firmware/src/keychain/radio.cpp firmware/src/keychain/sensors.cpp
    firmware/src/keychain/storage.cpp firmware/src/ssd1306.cpp)
  target_include_directories(keychain_board PRIVATE firmware/host/keychain firmware/src firmware/src/keychain)
  target_compile_options(keychain_board PRIVATE ${KC_SAN} -g -O1 -Wall)
  # Адрес буфера PWM пишется в 32-битный регистр: на ПК указатель шире.
  set_source_files_properties(firmware/src/keychain/hal.cpp PROPERTIES COMPILE_OPTIONS -fpermissive)
  target_link_options(keychain_board PRIVATE ${KC_SAN})
  target_link_libraries(keychain_board PRIVATE face_engine_san)
  add_executable(keychain_sim firmware/host/keychain/keychain_sim.cpp)
  target_link_libraries(keychain_sim PRIVATE face_sim_common)
  add_test(NAME keychain_sim COMMAND keychain_sim $<TARGET_FILE:keychain_board> ${CMAKE_BINARY_DIR}/keychain_sim_out)
  # Все сценарии подряд идут ~10 минут под санитайзерами; на медленной машине CI — дольше.
  set_tests_properties(keychain_sim PROPERTIES TIMEOUT 3600)
endif()

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
