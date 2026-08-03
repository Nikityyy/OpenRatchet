#pragma once

// VMA Configuration — must come before vulkan.h
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <SDL.h>
#include <SDL_vulkan.h>
#include <vector>

#include "openratchet/gs_state.h"
#include "openratchet/gif_parser.h"
#include "openratchet/gs_vram.h"

struct PS2Vertex {
    float x, y, z, w;
    float r, g, b, a;
    float u, v;
};

class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    bool Initialize(SDL_Window* window);
    void Shutdown();

    void BeginFrame();
    void EndFrame();
    void ProcessGIFPacket(const uint8_t* data, size_t size);

    // Accessors for debug overlay
    GS_State& GetGSState()    { return m_gsState; }
    GS_VRAM&  GetVRAM()       { return m_vram; }

    uint32_t GetFrameDrawCalls()   const { return m_frameDrawCalls; }
    uint32_t GetTotalDrawCalls()   const { return m_totalDrawCalls; }
    uint32_t GetFrameGIFPackets()  const { return m_frameGIFPackets; }
    uint32_t GetTotalGIFPackets()  const { return m_totalGIFPackets; }
    uint32_t GetGIFParsedPackets() const { return m_gifParser.GetPacketsProcessed(); }

private:
    bool CreateInstance();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateSwapchain(SDL_Window* window);
    bool CreateRenderPass();
    bool CreateFramebuffers();
    bool CreateCommandPool();
    bool CreateCommandBuffers();
    bool CreateSyncObjects();
    bool CreateAllocator();
    bool CreatePipeline();
    bool InitImGui();

    void DrawPrimitive(uint32_t prim_type, const std::vector<PS2Vertex>& vertices);

    // Core Vulkan
    SDL_Window*       m_window            = nullptr;
    VkInstance        m_instance          = VK_NULL_HANDLE;
    VkPhysicalDevice  m_physicalDevice    = VK_NULL_HANDLE;
    VkDevice          m_device            = VK_NULL_HANDLE;
    VkQueue           m_graphicsQueue     = VK_NULL_HANDLE;
    uint32_t          m_graphicsQueueFamily = (uint32_t)-1;
    VkSurfaceKHR      m_surface           = VK_NULL_HANDLE;

    // Swapchain
    VkSwapchainKHR             m_swapchain           = VK_NULL_HANDLE;
    VkFormat                   m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D                 m_swapchainExtent     = {};
    std::vector<VkImage>       m_swapchainImages;
    std::vector<VkImageView>   m_swapchainImageViews;
    std::vector<VkFramebuffer> m_swapchainFramebuffers;

    // Render pass / commands
    VkRenderPass    m_renderPass    = VK_NULL_HANDLE;
    VkCommandPool   m_commandPool   = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    // Synchronisation
    VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence     m_inFlightFence           = VK_NULL_HANDLE;
    uint32_t    m_imageIndex              = 0;

    // Memory allocator
    VmaAllocator  m_allocator              = VK_NULL_HANDLE;

    // Pipeline (created in Milestone 9)
    VkPipelineLayout m_pipelineLayout  = VK_NULL_HANDLE;
    VkPipeline       m_graphicsPipeline = VK_NULL_HANDLE;

    // Vertex buffer (staging)
    VkBuffer      m_vertexBuffer           = VK_NULL_HANDLE;
    VmaAllocation m_vertexBufferAllocation = VK_NULL_HANDLE;
    void*         m_vertexBufferMapped     = nullptr;
    size_t        m_vertexBufferSize       = 1024 * 1024;
    size_t        m_vertexBufferOffset     = 0;

    // GS subsystems
    GS_State   m_gsState;
    GIF_Parser m_gifParser;
    GS_VRAM    m_vram;

    // Per-frame / lifetime stats
    uint32_t m_frameDrawCalls  = 0;
    uint32_t m_totalDrawCalls  = 0;
    uint32_t m_frameGIFPackets = 0;
    uint32_t m_totalGIFPackets = 0;
    uint32_t m_texturesUploaded = 0;
};
