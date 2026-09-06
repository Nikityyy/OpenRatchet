#include "game/rac1_live_state.h"

#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void writeLe16(std::vector<std::uint8_t>& bytes,
               std::uint32_t address,
               std::uint16_t value) {
    bytes.at(address + 0u) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(address + 1u) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void writeLe32(std::vector<std::uint8_t>& bytes,
               std::uint32_t address,
               std::uint32_t value) {
    bytes.at(address + 0u) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(address + 1u) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes.at(address + 2u) = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes.at(address + 3u) = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void writeFloat(std::vector<std::uint8_t>& bytes,
                std::uint32_t address,
                float value) {
    writeLe32(bytes, address, std::bit_cast<std::uint32_t>(value));
}

void setPoolGlobals(std::vector<std::uint8_t>& bytes, std::uint32_t base) {
    using L = ratchet::game::Rac1LiveMobyLayout;
    writeLe32(bytes, L::kPoolBasePointerAddress, base);
    writeLe32(bytes,
              L::kPoolLastSlotPointerAddress,
              base + static_cast<std::uint32_t>((L::kCapacity - 1u) * L::kStride));
}

void testRetailTraversalAndAnimationLayout() {
    using L = ratchet::game::Rac1LiveMobyLayout;
    constexpr std::uint32_t base = 0x00180020u; // Deliberately not 0x100-aligned.
    std::vector<std::uint8_t> ram(0x00190000u, 0u);
    setPoolGlobals(ram, base);

    const auto slot = [&](std::size_t index) {
        return base + static_cast<std::uint32_t>(index * L::kStride);
    };

    // Slot 0: traversed non-player Moby.
    ram.at(slot(0) + L::kTraversalStateOffset) = 0u;
    writeLe32(ram, slot(0) + L::kClassPointerOffset, 0x00123400u);
    writeLe16(ram, slot(0) + L::kOClassOffset, 530u);
    writeLe32(ram, slot(0) + L::kPoolIndexOffset, 0u);
    writeFloat(ram, slot(0) + L::kWorldPositionOffset + 0u, 12.5f);
    writeFloat(ram, slot(0) + L::kWorldPositionOffset + 4u, -3.25f);
    writeFloat(ram, slot(0) + L::kWorldPositionOffset + 8u, 44.0f);
    writeFloat(ram, slot(0) + L::kRawModelScaleOffset, 1536.0f);
    writeFloat(ram, slot(0) + L::kRotationInputOffset + 0u, 0.125f);
    writeFloat(ram, slot(0) + L::kRotationInputOffset + 4u, -0.25f);
    writeFloat(ram, slot(0) + L::kRotationInputOffset + 8u, 3.1415927f);
    writeFloat(ram, slot(0) + L::kRotationBasisXOffset + 0u, 0.0f);
    writeFloat(ram, slot(0) + L::kRotationBasisXOffset + 4u, 1.0f);
    writeFloat(ram, slot(0) + L::kRotationBasisXOffset + 8u, 0.0f);
    writeFloat(ram, slot(0) + L::kRotationBasisYOffset + 0u, -1.0f);
    writeFloat(ram, slot(0) + L::kRotationBasisYOffset + 4u, 0.0f);
    writeFloat(ram, slot(0) + L::kRotationBasisYOffset + 8u, 0.0f);
    writeFloat(ram, slot(0) + L::kRotationBasisZOffset + 0u, 0.0f);
    writeFloat(ram, slot(0) + L::kRotationBasisZOffset + 4u, 0.0f);
    writeFloat(ram, slot(0) + L::kRotationBasisZOffset + 8u, 1.0f);
    ram.at(slot(0) + L::kFrameAOffset) = 8u;
    ram.at(slot(0) + L::kFrameBOffset) = 9u;
    ram.at(slot(0) + L::kSequenceAOffset) = 1u;
    ram.at(slot(0) + L::kSequenceBOffset) = 1u;
    writeFloat(ram, slot(0) + L::kInterpolationOffset, 0.375f);
    writeLe32(ram, slot(0) + L::kFramePointerAOffset, 0x00600000u);
    writeLe32(ram, slot(0) + L::kFramePointerBOffset, 0x00600400u);
    ram.at(slot(0) + L::kAnimationFlagsOffset) = 0x42u;

    // Slot 1: negative state other than -1 is skipped but does NOT terminate.
    ram.at(slot(1) + L::kTraversalStateOffset) = 0xfeu; // -2

    // Slot 2: traversed oClass 0 candidate (Ratchet).
    ram.at(slot(2) + L::kTraversalStateOffset) = 3u;
    writeLe32(ram, slot(2) + L::kClassPointerOffset, 0x00128000u);
    writeLe16(ram, slot(2) + L::kOClassOffset, 0u);
    writeLe32(ram, slot(2) + L::kPoolIndexOffset, 2u);
    ram.at(slot(2) + L::kFrameAOffset) = 9u;
    ram.at(slot(2) + L::kFrameBOffset) = 0u;
    ram.at(slot(2) + L::kSequenceAOffset) = 0u;
    ram.at(slot(2) + L::kSequenceBOffset) = 0u;
    writeFloat(ram, slot(2) + L::kInterpolationOffset, 0.625f);
    writeLe32(ram, slot(2) + L::kFramePointerAOffset, 0x00700000u);
    writeLe32(ram, slot(2) + L::kFramePointerBOffset, 0x00700540u);

    // Slot 3: exact retail traversal terminator. Bytes beyond it are poison and
    // must never be counted even when they resemble valid active records.
    ram.at(slot(3) + L::kTraversalStateOffset) = 0xffu; // -1
    ram.at(slot(4) + L::kTraversalStateOffset) = 0u;
    writeLe16(ram, slot(4) + L::kOClassOffset, 0u);

    const auto result = ratchet::game::inspectRac1LiveMobyPool(ram);
    assert(result.status == ratchet::game::Rac1LiveMobyPoolStatus::Ok);
    assert(result.poolBase == base);
    assert(result.poolLastSlot == base + 0x3f00u);
    assert(result.terminatorSlot == 3u);
    assert(result.slotsBeforeTerminator == 3u);
    assert(result.records.size() == 3u);
    assert(result.traversedMobyCount == 2u);
    assert(result.skippedNegativeStateCount == 1u);
    assert(result.ratchetCandidateCount == 1u);

    const auto& moby = result.records.at(0);
    assert(moby.slotIndex == 0u);
    assert(moby.guestAddress == base);
    assert(moby.participatesInRetailTraversal);
    assert(moby.classPointer == 0x00123400u);
    assert(moby.oClass == 530);
    assert(moby.storedPoolIndex == 0u);
    assert(std::abs(moby.worldTransform.position[0] - 12.5f) < 1.0e-7f);
    assert(std::abs(moby.worldTransform.position[1] + 3.25f) < 1.0e-7f);
    assert(std::abs(moby.worldTransform.position[2] - 44.0f) < 1.0e-7f);
    assert(std::abs(moby.worldTransform.rawModelScale - 1536.0f) < 1.0e-7f);
    assert(std::abs(moby.worldTransform.rotationInput[0] - 0.125f) < 1.0e-7f);
    assert(std::abs(moby.worldTransform.rotationInput[1] + 0.25f) < 1.0e-7f);
    assert(std::abs(moby.worldTransform.rotationInput[2] - 3.1415927f) < 1.0e-6f);
    assert((moby.worldTransform.basisX == std::array<float, 3>{0.0f, 1.0f, 0.0f}));
    assert((moby.worldTransform.basisY == std::array<float, 3>{-1.0f, 0.0f, 0.0f}));
    assert((moby.worldTransform.basisZ == std::array<float, 3>{0.0f, 0.0f, 1.0f}));
    assert(moby.animation.frameA == 8u);
    assert(moby.animation.frameB == 9u);
    assert(moby.animation.sequenceA == 1u);
    assert(moby.animation.sequenceB == 1u);
    assert(std::abs(moby.animation.interpolation - 0.375f) < 1.0e-7f);
    assert(moby.animation.framePointerA == 0x00600000u);
    assert(moby.animation.framePointerB == 0x00600400u);
    assert(moby.animation.flags == 0x42u);

    const auto& skipped = result.records.at(1);
    assert(skipped.traversalState == -2);
    assert(!skipped.participatesInRetailTraversal);

    const auto& ratchet = result.records.at(2);
    assert(ratchet.participatesInRetailTraversal);
    assert(ratchet.oClass == 0);
    assert(ratchet.storedPoolIndex == 2u);
    assert(ratchet.animation.frameA == 9u);
    assert(ratchet.animation.frameB == 0u);
    assert(std::abs(ratchet.animation.interpolation - 0.625f) < 1.0e-7f);
}

void testPoolNotInitialized() {
    std::vector<std::uint8_t> ram(0x00170000u, 0u);
    const auto result = ratchet::game::inspectRac1LiveMobyPool(ram);
    assert(result.status == ratchet::game::Rac1LiveMobyPoolStatus::PoolNotInitialized);
}

void testLastSlotContract() {
    using L = ratchet::game::Rac1LiveMobyLayout;
    constexpr std::uint32_t base = 0x00180000u;
    std::vector<std::uint8_t> ram(0x00190000u, 0u);
    writeLe32(ram, L::kPoolBasePointerAddress, base);
    writeLe32(ram, L::kPoolLastSlotPointerAddress, base + 0x3e00u);
    const auto result = ratchet::game::inspectRac1LiveMobyPool(ram);
    assert(result.status == ratchet::game::Rac1LiveMobyPoolStatus::PoolLastSlotMismatch);
}

void testMissingTerminatorHardFails() {
    using L = ratchet::game::Rac1LiveMobyLayout;
    constexpr std::uint32_t base = 0x00180000u;
    std::vector<std::uint8_t> ram(0x00190000u, 0u);
    setPoolGlobals(ram, base);
    // Zero is traversed by retail. Deliberately leave all 64 state bytes zero.
    const auto result = ratchet::game::inspectRac1LiveMobyPool(ram);
    assert(result.status ==
           ratchet::game::Rac1LiveMobyPoolStatus::MissingTraversalTerminator);
    assert(result.records.size() == L::kCapacity);
    assert(result.traversedMobyCount == L::kCapacity);
}

void testOutOfRangePoolHardFails() {
    std::vector<std::uint8_t> ram(0x00170000u, 0u);
    constexpr std::uint32_t base = 0x0016f000u;
    setPoolGlobals(ram, base);
    const auto result = ratchet::game::inspectRac1LiveMobyPool(ram);
    assert(result.status == ratchet::game::Rac1LiveMobyPoolStatus::PoolRangeOutOfRange);
}

} // namespace

int main() {
    testRetailTraversalAndAnimationLayout();
    testPoolNotInitialized();
    testLastSlotContract();
    testMissingTerminatorHardFails();
    testOutOfRangePoolHardFails();

    assert(std::string_view(ratchet::game::rac1LiveMobyPoolStatusName(
               ratchet::game::Rac1LiveMobyPoolStatus::Ok)) == "ok");
    std::cout << "R&C1 live Moby state contract tests passed\n";
    return 0;
}
