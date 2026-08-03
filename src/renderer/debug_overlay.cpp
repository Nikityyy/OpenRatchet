#include "openratchet/vulkan_renderer.h"
#include <imgui.h>

namespace OpenRatchet {
namespace Debug {

void RenderOverlay(VulkanRenderer& renderer, uint32_t ee_calls) {
    ImGuiIO& io = ImGui::GetIO();

    // ── Main stats window ────────────────────────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 260), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.75f);

    if (ImGui::Begin("OpenRatchet Debug Overlay")) {
        // Performance
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Performance");
        ImGui::Separator();
        ImGui::Text("FPS:        %.1f", io.Framerate);
        ImGui::Text("Frame time: %.3f ms", 1000.0f / io.Framerate);

        ImGui::Spacing();

        // GIF / GS stats
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "GIF / GS");
        ImGui::Separator();
        ImGui::Text("GIF packets (this frame): %u", renderer.GetFrameGIFPackets());
        ImGui::Text("GIF packets (total):      %u", renderer.GetTotalGIFPackets());
        ImGui::Text("GIF packets (parsed):     %u", renderer.GetGIFParsedPackets());
        ImGui::Text("Draw calls  (this frame): %u", renderer.GetFrameDrawCalls());
        ImGui::Text("Draw calls  (total):      %u", renderer.GetTotalDrawCalls());

        ImGui::Spacing();

        // GS register state
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "GS Register Snapshot");
        ImGui::Separator();
        auto& gs = renderer.GetGSState();
        ImGui::Text("PRIM:     0x%016llX", (unsigned long long)gs.PRIM);
        ImGui::Text("TEX0_1:   0x%016llX", (unsigned long long)gs.TEX0_1);
        ImGui::Text("FRAME_1:  0x%016llX", (unsigned long long)gs.FRAME_1);
        ImGui::Text("RGBAQ:    0x%016llX", (unsigned long long)gs.RGBAQ);
        ImGui::Text("SCISSOR1: 0x%016llX", (unsigned long long)gs.SCISSOR_1);
        ImGui::Text("ALPHA_1:  0x%016llX", (unsigned long long)gs.ALPHA_1);

        ImGui::Spacing();

        // EE dispatch count
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 1.0f, 1.0f), "EE Recompiler");
        ImGui::Separator();
        ImGui::Text("EE dispatch calls: %u", ee_calls);
    }
    ImGui::End();
}

} // namespace Debug
} // namespace OpenRatchet
