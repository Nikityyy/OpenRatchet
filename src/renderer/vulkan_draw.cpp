// vulkan_draw.cpp — Primitive rendering and frame management
#include "openratchet/vulkan_renderer.h"
#include <iostream>
#include <cstring>
#include <fstream>
#include <vector>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>

// ─── SPIR-V loading helper ────────────────────────────────────────────────────

static std::vector<uint32_t> LoadSPIRV(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const size_t size = static_cast<size_t>(f.tellg());
    if (size == 0 || size % 4 != 0) return {};
    f.seekg(0);
    std::vector<uint32_t> code(size / 4);
    f.read(reinterpret_cast<char*>(code.data()), size);
    return code;
}

// ─── Pipeline creation ────────────────────────────────────────────────────────

bool VulkanRenderer::CreatePipeline() {
#ifndef OPENRATCHET_SHADER_DIR
    std::cerr << "[Vulkan] OPENRATCHET_SHADER_DIR not defined — skipping pipeline\n";
    return true; // non-fatal
#else
    const std::string shaderDir = OPENRATCHET_SHADER_DIR;
    if (shaderDir.empty()) {
        std::cerr << "[Vulkan] Shader directory empty (glslc not found at CMake time) — no game geometry will render\n";
        return true; // non-fatal
    }

    const auto vertCode = LoadSPIRV((shaderDir + "ps2_prim.vert.spv").c_str());
    const auto fragCode = LoadSPIRV((shaderDir + "ps2_prim.frag.spv").c_str());
    if (vertCode.empty() || fragCode.empty()) {
        std::cerr << "[Vulkan] Failed to load compiled shaders from: " << shaderDir << "\n";
        return true; // non-fatal — ImGui still works
    }

    auto makeModule = [&](const std::vector<uint32_t>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo info{};
        info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size() * 4;
        info.pCode    = code.data();
        VkShaderModule mod = VK_NULL_HANDLE;
        vkCreateShaderModule(m_device, &info, nullptr, &mod);
        return mod;
    };
    VkShaderModule vertMod = makeModule(vertCode);
    VkShaderModule fragMod = makeModule(fragCode);
    if (!vertMod || !fragMod) {
        std::cerr << "[Vulkan] Failed to create shader modules\n";
        vkDestroyShaderModule(m_device, vertMod, nullptr);
        vkDestroyShaderModule(m_device, fragMod, nullptr);
        return true;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    // Vertex layout: PS2Vertex = {x,y,z,w, r,g,b,a, u,v} — 10 floats = 40 bytes
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(PS2Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32A32_SFLOAT; // position (x,y,z,w)
    attrs[0].offset   = offsetof(PS2Vertex, x);
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT; // color (r,g,b,a)
    attrs[1].offset   = offsetof(PS2Vertex, r);
    attrs[2].location = 2; attrs[2].binding = 0;
    attrs[2].format   = VK_FORMAT_R32G32_SFLOAT;       // texcoord (u,v)
    attrs[2].offset   = offsetof(PS2Vertex, u);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = VK_CULL_MODE_NONE; // PS2 doesn't cull by default
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable         = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments    = &blendAttachment;

    // Dynamic viewport and scissor so we can handle resize
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynamicStates;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        std::cerr << "[Vulkan] Failed to create pipeline layout\n";
        vkDestroyShaderModule(m_device, vertMod, nullptr);
        vkDestroyShaderModule(m_device, fragMod, nullptr);
        return true;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = m_pipelineLayout;
    pipelineInfo.renderPass          = m_renderPass;
    pipelineInfo.subpass             = 0;

    const VkResult res = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                                    &pipelineInfo, nullptr, &m_graphicsPipeline);
    vkDestroyShaderModule(m_device, vertMod, nullptr);
    vkDestroyShaderModule(m_device, fragMod, nullptr);

    if (res != VK_SUCCESS) {
        std::cerr << "[Vulkan] Failed to create graphics pipeline (VkResult=" << res << ")\n";
        return true; // non-fatal — fall back to ImGui only
    }

    // Create the persistent host-visible vertex buffer (1 MB, mapped persistently)
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size  = m_vertexBufferSize;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VmaAllocationInfo allocResult{};
    if (vmaCreateBuffer(m_allocator, &bufInfo, &allocInfo,
                        &m_vertexBuffer, &m_vertexBufferAllocation, &allocResult) != VK_SUCCESS) {
        std::cerr << "[Vulkan] Failed to create vertex buffer\n";
        return true;
    }
    m_vertexBufferMapped = allocResult.pMappedData;

    std::cout << "[Vulkan] Graphics pipeline and vertex buffer ready.\n";
    return true;
#endif
}

// ─── Frame begin/end ─────────────────────────────────────────────────────────

void VulkanRenderer::BeginFrame() {
    if (m_device == VK_NULL_HANDLE || m_swapchain == VK_NULL_HANDLE) return;

    vkWaitForFences(m_device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);

    const VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                                   m_imageAvailableSemaphore, VK_NULL_HANDLE,
                                                   &m_imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) return;
    if (result != VK_SUCCESS) { std::cerr << "vkAcquireNextImageKHR failed\n"; return; }

    vkResetFences(m_device, 1, &m_inFlightFence);
    vkResetCommandBuffer(m_commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

    VkClearValue clearColor = {{{0.05f, 0.05f, 0.12f, 1.0f}}};
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass        = m_renderPass;
    rpInfo.framebuffer       = m_swapchainFramebuffers[m_imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_swapchainExtent;
    rpInfo.clearValueCount   = 1;
    rpInfo.pClearValues      = &clearColor;
    vkCmdBeginRenderPass(m_commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set dynamic viewport and scissor
    VkViewport viewport{};
    viewport.x        = 0.0f; viewport.y = 0.0f;
    viewport.width    = static_cast<float>(m_swapchainExtent.width);
    viewport.height   = static_cast<float>(m_swapchainExtent.height);
    viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchainExtent;
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);

    // Reset per-frame vertex write cursor
    m_vertexBufferOffset = 0;

    // ImGui new frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    m_frameDrawCalls  = 0;
    m_frameGIFPackets = 0;
}

void VulkanRenderer::EndFrame() {
    if (m_device == VK_NULL_HANDLE || m_swapchain == VK_NULL_HANDLE) return;

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_commandBuffer);

    vkCmdEndRenderPass(m_commandBuffer);
    vkEndCommandBuffer(m_commandBuffer);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &m_imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask    = &waitStage;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &m_commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &m_renderFinishedSemaphore;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFence);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &m_renderFinishedSemaphore;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &m_swapchain;
    presentInfo.pImageIndices      = &m_imageIndex;
    vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
}

// ─── GIF packet processing ────────────────────────────────────────────────────

void VulkanRenderer::ProcessGIFPacket(const uint8_t* data, size_t size) {
    auto cb = [this](uint8_t prim_type, const std::vector<GIF_Vertex>& verts) {
        // XYOFFSET is stored in 12:4 fixed-point format (bits 0-15 = X, 32-47 = Y).
        // e.g. center 2048.0 is represented as 32768.
        const uint64_t xyoffset = m_gsState.XYOFFSET_1;
        const float ofx = static_cast<float>((xyoffset >> 0) & 0xFFFF) / 16.0f;
        const float ofy = static_cast<float>((xyoffset >> 32) & 0xFFFF) / 16.0f;

        // Convert GIF_Vertex → PS2Vertex (NDC coords from 4-bit fixed-point PS2 space)
        std::vector<PS2Vertex> ps2verts;
        ps2verts.reserve(verts.size());
        
        // Typical resolution for display mapping.
        constexpr float W = 640.0f, H = 448.0f;
        
        for (const auto& gv : verts) {
            PS2Vertex v;
            // Convert from 4-bit fixed point to float pixels
            float px = static_cast<float>(gv.x) / 16.0f;
            float py = static_cast<float>(gv.y) / 16.0f;
            
            // Subtract the GS drawing offset
            px -= ofx;
            py -= ofy;
            
            // Map offset-relative [0, W] and [0, H] to NDC [-1, 1]
            v.x = (px / W) * 2.0f - 1.0f;
            v.y = (py / H) * 2.0f - 1.0f;
            
            // Z is 32-bit (24-bit usable), map to [0, 1]
            v.z = static_cast<float>(gv.z) / static_cast<float>(0xFFFFFFu);
            v.w = 1.0f;
            
            v.r = gv.r / 255.0f; v.g = gv.g / 255.0f;
            v.b = gv.b / 255.0f; v.a = gv.a / 255.0f;
            v.u = gv.s; v.v = gv.t;
            ps2verts.push_back(v);
        }
        DrawPrimitive(prim_type, ps2verts);
    };

    m_gifParser.ParsePacket(m_gsState, &m_vram, data, size, cb);
    ++m_frameGIFPackets;
    ++m_totalGIFPackets;
}

// ─── Primitive drawing ────────────────────────────────────────────────────────

void VulkanRenderer::DrawPrimitive(uint32_t prim_type,
                                    const std::vector<PS2Vertex>& vertices) {
    if (!m_graphicsPipeline || !m_vertexBufferMapped || vertices.empty()) {
        ++m_frameDrawCalls; ++m_totalDrawCalls;
        return;
    }

    // Triangulate: PS2 types 3=triangle, 4=tri-strip, 5=tri-fan, 6=sprite(quad)
    // Build a flat triangle list for Vulkan (TRIANGLE_LIST topology).
    std::vector<PS2Vertex> tris;
    tris.reserve(vertices.size());

    const size_t n = vertices.size();
    switch (prim_type & 7) {
        case 3: // Triangle list — copy as-is
            tris = vertices;
            break;
        case 4: // Triangle strip
            for (size_t i = 2; i < n; ++i) {
                if (i & 1) { tris.push_back(vertices[i-1]); tris.push_back(vertices[i-2]); }
                else        { tris.push_back(vertices[i-2]); tris.push_back(vertices[i-1]); }
                tris.push_back(vertices[i]);
            }
            break;
        case 5: // Triangle fan
            for (size_t i = 2; i < n; ++i) {
                tris.push_back(vertices[0]);
                tris.push_back(vertices[i-1]);
                tris.push_back(vertices[i]);
            }
            break;
        case 6: // Sprite (screen-aligned quad) — 2 corner verts → 2 triangles
            for (size_t i = 1; i < n; i += 2) {
                const auto& a = vertices[i-1];
                const auto& b = vertices[i];
                PS2Vertex bl = a; bl.y = b.y;
                PS2Vertex tr = b; tr.y = a.y;
                tris.push_back(a); tris.push_back(bl); tris.push_back(b);
                tris.push_back(a); tris.push_back(b);  tris.push_back(tr);
            }
            break;
        default: // Points, lines — skip for now
            ++m_frameDrawCalls; ++m_totalDrawCalls;
            return;
    }

    if (tris.empty()) { ++m_frameDrawCalls; ++m_totalDrawCalls; return; }

    const size_t byteSize = tris.size() * sizeof(PS2Vertex);
    if (m_vertexBufferOffset + byteSize > m_vertexBufferSize) {
        // Vertex buffer full this frame — reset (accept tearing over crash)
        m_vertexBufferOffset = 0;
    }

    // Write vertices into the persistently mapped buffer
    uint8_t* dst = static_cast<uint8_t*>(m_vertexBufferMapped) + m_vertexBufferOffset;
    std::memcpy(dst, tris.data(), byteSize);

    // Record draw call into the current command buffer
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    const VkDeviceSize offset = static_cast<VkDeviceSize>(m_vertexBufferOffset);
    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &m_vertexBuffer, &offset);
    vkCmdDraw(m_commandBuffer, static_cast<uint32_t>(tris.size()), 1, 0, 0);

    m_vertexBufferOffset += byteSize;
    ++m_frameDrawCalls;
    ++m_totalDrawCalls;
}
