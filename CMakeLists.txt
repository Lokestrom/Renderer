cmake_minimum_required (VERSION 4.1)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB cppFiles
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(vulkanInterface ${cppFiles})

target_link_libraries(vulkanInterface PUBLIC imgui external)

target_include_directories(vulkanInterface PUBLIC ${CMAKE_SOURCE_DIR})

add_compile_definitions(APP_USE_UNLIMITED_FRAME_RATE VULKAN_HPP_NO_CONSTRUCTORS)