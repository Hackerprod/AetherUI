cmake_minimum_required(VERSION 3.16)
project(AetherUIPackageConsumer LANGUAGES C)

find_package(AetherUI 0.1 CONFIG REQUIRED)

add_executable(aether_package_consumer main.c)
target_link_libraries(aether_package_consumer PRIVATE AetherUI::Core)
set_target_properties(aether_package_consumer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

if(WIN32)
    add_custom_command(
        TARGET aether_package_consumer
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:AetherUI::Core>"
                "$<TARGET_FILE_DIR:aether_package_consumer>"
        VERBATIM
    )
endif()

enable_testing()
add_test(NAME aether.package-consumer COMMAND aether_package_consumer)
