#include "openratchet/vulkan_renderer.h"
#include <iostream>
#include <cstring>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>

// ─── Frame begin/end ─────────────────────────────────────────────────────────

void VulkanRenderer::BeginFrame() {
    if (m_device == VK_NULL_HANDLE || m_swapchain == VK_NULL_HANDLE) return;

    vkWaitForFences(m_device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
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

    // ImGui new frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Reset per-frame draw call counter
    m_frameDrawCalls = 0;
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
    // Wire the DrawCallback so vertex accumulation calls DrawPrimitive
    auto cb = [this](uint8_t prim_type, const std::vector<GIF_Vertex>& verts) {
        // Convert GIF_Vertex → PS2Vertex
        std::vector<PS2Vertex> ps2verts;
        ps2verts.reserve(verts.size());
        // XYOFFSET is in 4-bit subpixel units (1/16 of a pixel)
        // Convert to NDC assuming 640×448 PS2 frame buffer
        constexpr float W = 640.0f, H = 448.0f;
        for (auto& gv : verts) {
            PS2Vertex v;
            float px = (gv.x / 16.0f) / W * 2.0f - 1.0f;
            float py = (gv.y / 16.0f) / H * 2.0f - 1.0f;
            float pz = gv.z / (float)0xFFFFFF;
            v.x = px; v.y = py; v.z = pz; v.w = 1.0f;
            v.r = gv.r / 255.0f; v.g = gv.g / 255.0f;
            v.b = gv.b / 255.0f; v.a = gv.a / 255.0f;
            v.u = gv.s; v.v = gv.t;
            ps2verts.push_back(v);
        }
        DrawPrimitive(prim_type, ps2verts);
    };

    m_gifParser.ParsePacket(m_gsState, data, size, cb);
    m_frameGIFPackets++;
    m_totalGIFPackets++;
}

// ─── Primitive drawing ────────────────────────────────────────────────────────

void VulkanRenderer::DrawPrimitive(uint32_t /*prim_type*/, const std::vector<PS2Vertex>& vertices) {
    // Milestone 8: log the call; actual vertex-buffer submission will come in Milestone 9+
    (void)vertices;
    m_frameDrawCalls++;
    m_totalDrawCalls++;
}

// ─── Pipeline ─────────────────────────────────────────────────────────────────

bool VulkanRenderer::CreatePipeline() {
    // Deferred to Milestone 9 when we have compiled SPIR-V shaders.
    // Returning true so init doesn't fail.
    return true;
}
