#pragma once
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

// Uploaded texture handle for a specific GS VRAM region
struct GS_Texture {
    uint32_t base_ptr;
    uint32_t width;
    uint32_t height;
    uint32_t psm;
    VkImage      image       = VK_NULL_HANDLE;
    VkImageView  imageView   = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

class GS_VRAM {
public:
    GS_VRAM();
    ~GS_VRAM();

    void Init();

    // Write raw image data into VRAM at a block-pointer address
    void WriteImage(uint32_t base_ptr, uint32_t width, uint32_t psm, const uint8_t* data, size_t size);

    // Read a texture from VRAM into a 32-bit RGBA buffer (for uploading to Vulkan)
    void ReadTexture32(uint32_t base_ptr, uint32_t tbw, uint32_t width, uint32_t height, uint32_t psm,
                       std::vector<uint32_t>& out_rgba) const;

    // Convert a byte offset from a block pointer (256-byte blocks)
    static uint32_t BlockPtrToOffset(uint32_t base_ptr) { return base_ptr * 256; }

    uint8_t* Data() { return m_vram.data(); }
    size_t   Size() const { return m_vram.size(); }

private:
    std::vector<uint8_t> m_vram;
};
