cmake_minimum_required(VERSION 3.16)
project(ai_wroom_32_sim C)

set(CMAKE_C_STANDARD 11)

include(FetchContent)
FetchContent_Declare(
  lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.5.0
)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl)

find_package(SDL2 REQUIRED)
if(TARGET SDL2::SDL2)
  set(SIM_SDL_TARGET SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
  set(SIM_SDL_TARGET SDL2::SDL2-static)
endif()
if(SIM_SDL_TARGET)
  target_link_libraries(lvgl PUBLIC ${SIM_SDL_TARGET})
else()
  target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
  target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
endif()

add_executable(sim
  main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../ui/ui.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../ui/attraction.c
)
target_include_directories(sim PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../ui/include
)
target_compile_definitions(sim PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(sim PRIVATE lvgl::lvgl)
