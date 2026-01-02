cmake_minimum_required(VERSION 3.28)
project("Counterforce: Global Conflict" LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

find_package(Vulkan REQUIRED)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

add_library(glm_header INTERFACE)
target_include_directories(glm_header INTERFACE external/glm)

set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})

set(GLSLC_EXE "")
if (TARGET Vulkan::glslc)
  set(GLSLC_EXE Vulkan::glslc)
else()
  find_program(GLSLC_EXE glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)
endif()
if (NOT GLSLC_EXE)
  message(FATAL_ERROR "glslc not found")
endif()

set(SPVS "")
function(compile_shader SRC)
  get_filename_component(FILE ${SRC} NAME)
  set(OUT ${SHADER_OUT}/${FILE}.spv)
  add_custom_command(
    OUTPUT ${OUT}
    COMMAND ${GLSLC_EXE} -O ${SRC} -o ${OUT}
    MAIN_DEPENDENCY ${SRC}
    VERBATIM
  )
  list(APPEND SPVS ${OUT})
  set(SPVS "${SPVS}" PARENT_SCOPE)
endfunction()

compile_shader(${SHADER_DIR}/sky.vert)
compile_shader(${SHADER_DIR}/sky.frag)
compile_shader(${SHADER_DIR}/mesh.vert)
compile_shader(${SHADER_DIR}/mesh.frag)
compile_shader(${SHADER_DIR}/cull.comp)

list(REMOVE_DUPLICATES SPVS)
add_custom_target(Shaders ALL DEPENDS ${SPVS})

add_library(engine STATIC
  src/engine/gfx/Mesh.cpp
  src/engine/gfx/Renderer.cpp
  src/engine/gfx/RenderGraph.cpp
  src/engine/gfx/UploadManager.cpp
  src/engine/gfx/VulkanContext.cpp
  src/engine/gfx/VulkanHelpers.cpp
  src/engine/assets/GltfLoader.cpp
  src/engine/assets/ImageLoaderWIC.cpp
  src/engine/assets/ObjLoader.cpp
  src/engine/platform/Input.cpp
)

add_executable(CSOS
  src/game/App.cpp
  src/game/Camera.cpp
  src/game/main.cpp
)

add_dependencies(CSOS Shaders)

target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC Vulkan::Vulkan glfw glm_header)
target_link_libraries(CSOS PRIVATE engine)
if (WIN32)
  target_link_libraries(engine PRIVATE windowscodecs)
endif()
target_compile_definitions(engine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(engine PRIVATE
  $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:CFGC_DIAGNOSTICS=1>
)

set_property(TARGET engine PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set_property(TARGET CSOS PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

target_compile_options(engine PRIVATE
  /W4 /permissive- /EHsc
  /Zc:__cplusplus /Zc:inline

  $<$<CONFIG:Debug>:/Od /Ob0 /Zi /RTC1 /Oy->
  $<$<CONFIG:RelWithDebInfo>:/O2 /Ob2 /Zi /DNDEBUG>
  $<$<CONFIG:Release>:/O2 /Ob2 /DNDEBUG>

  $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Gy /Gw>
)

target_link_options(engine PRIVATE
  $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/DEBUG:FULL>
  $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/OPT:REF /OPT:ICF /INCREMENTAL:NO>
)

option(LTO "Enable LTO in Release/RelWithDebInfo" ON)
if (LTO AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(engine PRIVATE
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-flto>
  )
  target_link_options(engine PRIVATE
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-flto>
  )
endif()

option(ASAN "Enable AddressSanitizer in Debug (clang-cl)" OFF)
if (ASAN AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(engine PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
  target_link_options(engine PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
endif()

add_custom_command(TARGET CSOS POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:CSOS>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${SHADER_OUT}
          $<TARGET_FILE_DIR:CSOS>/shaders
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:CSOS>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets
          $<TARGET_FILE_DIR:CSOS>/assets
)