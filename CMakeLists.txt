cmake_minimum_required(VERSION 3.16)
project(LumenCreator LANGUAGES CXX)

# 1. C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)

# 2. Find System Dependencies
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

# 3. Define the Executable
file(GLOB_RECURSE LUMENCREATOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(LumenCreator ${LUMENCREATOR_SOURCES})

# 4. Include Directories
target_include_directories(LumenCreator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lumen-inc
    ${Vulkan_INCLUDE_DIRS}
)

# 5. VulkanEngine shared library
add_library(VulkanEngine SHARED IMPORTED)
set_target_properties(VulkanEngine PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanEngine.so"
)

# 6. Linking
target_link_libraries(LumenCreator PRIVATE
    VulkanEngine
    glfw
    Vulkan::Vulkan
)

# 7. RPATH so it finds the .so without LD_LIBRARY_PATH
set_target_properties(LumenCreator PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/external"
)
