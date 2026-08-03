add_library(openratchet_renderer STATIC 
    gs_state.cpp 
    gif_parser.cpp 
    vulkan_init.cpp 
    vulkan_draw.cpp
    gs_vram.cpp
    vma_impl.cpp
    debug_overlay.cpp
    
    # ImGui sources
    ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_sdl2.cpp
    ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(openratchet_renderer PUBLIC 
    ${CMAKE_SOURCE_DIR}/third_party/imgui
    ${CMAKE_SOURCE_DIR}/third_party/vma
)
if(OPENRATCHET_SKIP_DEPS)
    target_include_directories(openratchet_renderer PUBLIC ${OPENRATCHET_SDL2_INCLUDE_DIR})
endif()

if(NOT OPENRATCHET_SKIP_DEPS)
    target_link_libraries(openratchet_renderer PUBLIC Vulkan::Vulkan SDL2::SDL2)
endif()
