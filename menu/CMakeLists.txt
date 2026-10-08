# Builds the menu as a small library you link into your payload.
# You must supply Dear ImGui yourself:  cmake -DIMGUI_DIR=/path/to/imgui ...
# NOTE: this file has not been run yet (the sandbox has no Android NDK or ImGui).
cmake_minimum_required(VERSION 3.18)
project(tzmenu CXX)
set(CMAKE_CXX_STANDARD 17)

if(NOT IMGUI_DIR OR NOT EXISTS "${IMGUI_DIR}/imgui.h")
  message(FATAL_ERROR "Set IMGUI_DIR to the folder containing imgui.h (download Dear ImGui from github.com/ocornut/imgui)")
endif()

add_library(tzmenu STATIC
  src/menu_input.cpp src/theme.cpp src/settings.cpp src/menu_ui.cpp src/tz_menu.cpp src/integration_stubs.cpp
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp)
target_include_directories(tzmenu PUBLIC src ${IMGUI_DIR})
