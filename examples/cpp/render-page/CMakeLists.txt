cmake_minimum_required(VERSION 3.16)
project(AetherUIRenderPage LANGUAGES C)

find_package(AetherUI CONFIG REQUIRED)

add_executable(aetherui_render_page main.c)
target_link_libraries(aetherui_render_page PRIVATE AetherUI::Core)
