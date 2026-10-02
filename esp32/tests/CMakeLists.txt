cmake_minimum_required(VERSION 3.16)
project(provider_parser_tests C)
enable_testing()

set(CJSON_DIR "" CACHE PATH "Path to the cJSON source directory")
if(NOT EXISTS "${CJSON_DIR}/cJSON.c")
  message(FATAL_ERROR "Set -DCJSON_DIR=/path/to/cJSON (containing cJSON.c)")
endif()

add_executable(provider_parser_test provider_parser_test.c
  ../firmware/main/provider_parser.c ${CJSON_DIR}/cJSON.c)
target_include_directories(provider_parser_test PRIVATE ../firmware/main ../ui/include ${CJSON_DIR})
target_compile_options(provider_parser_test PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
set_source_files_properties(${CJSON_DIR}/cJSON.c PROPERTIES
  COMPILE_OPTIONS "-Wno-error=deprecated-declarations")
target_link_options(provider_parser_test PRIVATE -fsanitize=address,undefined)
add_test(NAME provider_parser_test COMMAND provider_parser_test)

add_executable(screen_config_test screen_config_test.c
  ../firmware/main/screen_config.c ../ui/providers.c ${CJSON_DIR}/cJSON.c)
target_include_directories(screen_config_test PRIVATE
  hostinclude ../firmware/main ../ui/include ${CJSON_DIR})
target_compile_options(screen_config_test PRIVATE -Wall -Wextra -Werror
  -fsanitize=address,undefined -fno-omit-frame-pointer)
set_source_files_properties(${CJSON_DIR}/cJSON.c PROPERTIES
  COMPILE_OPTIONS "-Wno-error=deprecated-declarations")
target_link_options(screen_config_test PRIVATE -fsanitize=address,undefined)
add_test(NAME screen_config_test COMMAND screen_config_test)

add_executable(gc9b72_test gc9b72_test.c ../firmware/main/gc9b72.c)
target_include_directories(gc9b72_test PRIVATE hostinclude ../firmware/main)
target_compile_options(gc9b72_test PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(gc9b72_test PRIVATE -fsanitize=address,undefined)
add_test(NAME gc9b72_test COMMAND gc9b72_test)
