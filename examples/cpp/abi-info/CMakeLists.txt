cmake_minimum_required(VERSION 3.16)
project(AetherUIAbiInfo LANGUAGES C)

find_package(AetherUI CONFIG REQUIRED)

add_executable(aetherui_abi_info main.c)
target_link_libraries(aetherui_abi_info PRIVATE AetherUI::Core)
