#include "game/rac1_live_state.h"
#include "game/rac1_live_transform.h"

#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

using Layout = ratchet::game::Rac1LiveMobyLayout;

static_assert(std::bit_cast<std::uint32_t>(ratchet::game::kRac1LiveMobyRawScaleToWorld) ==
              0x3a800000u);

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

void writeVec3(std::vector<std::uint8_t>& bytes,
               std::uint32_t address,
               const std::array<float, 3>& value) {
    for (std::size_t axis = 0u; axis < value.size(); ++axis) {
        writeFloat(bytes,
                   address + static_cast<std::uint32_t>(axis * sizeof(float)),
                   value[axis]);
    }
}

void setPoolGlobals(std::vector<std::uint8_t>& bytes, std::uint32_t base) {
    writeLe32(bytes, Layout::kPoolBasePointerAddress, base);
    writeLe32(bytes,
              Layout::kPoolLastSlotPointerAddress,
              base + static_cast<std::uint32_t>((Layout::kCapacity - 1u) * Layout::kStride));
}

struct Fixture {
    static constexpr std::uint32_t kBase = 0x00180000u;
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x00190000u, 0u);

    Fixture() {
        setPoolGlobals(ram, kBase);
        const std::uint32_t ratchet = slot(0u);
        ram.at(ratchet + Layout::kTraversalStateOffset) = 0u;
        writeLe16(ram, ratchet + Layout::kOClassOffset, 0u);
        writeLe32(ram, ratchet + Layout::kClassPointerOffset, 0x00170000u);
        writeLe32(ram, ratchet + Layout::kPoolIndexOffset, 0u);
        writeVec3(ram, ratchet + Layout::kWorldPositionOffset, {10.0f, 20.0f, 30.0f});
        writeFloat(ram, ratchet + Layout::kRawModelScaleOffset, 2048.0f);
        writeVec3(ram, ratchet + Layout::kRotationInputOffset, {0.0f, 0.0f, 1.5707964f});
        writeVec3(ram, ratchet + Layout::kRotationBasisXOffset, {0.0f, 1.0f, 0.0f});
        writeVec3(ram, ratchet + Layout::kRotationBasisYOffset, {-1.0f, 0.0f, 0.0f});
        writeVec3(ram, ratchet + Layout::kRotationBasisZOffset, {0.0f, 0.0f, 1.0f});
        ram.at(slot(1u) + Layout::kTraversalStateOffset) = 0xffu;
    }

    [[nodiscard]] static std::uint32_t slot(std::size_t index) {
        return kBase + static_cast<std::uint32_t>(index * Layout::kStride);
    }
};

ratchet::game::Rac1LiveRatchetTransformResult inspect(const Fixture& fixture) {
    const auto pool = ratchet::game::inspectRac1LiveMobyPool(fixture.ram);
    assert(pool.status == ratchet::game::Rac1LiveMobyPoolStatus::Ok);
    return ratchet::game::inspectRac1LiveRatchetWorldTransform(pool);
}

void testRetailWorldFormulaAndBasisColumns() {
    const Fixture fixture;
    const auto result = inspect(fixture);
    assert(result.ok());
    assert(result.ratchetCandidates == 1u);
    assert(result.transform.mobyGuestAddress == Fixture::kBase);
    assert(result.transform.oClass == 0);
    assert((result.transform.position == std::array<float, 3>{10.0f, 20.0f, 30.0f}));
    assert(result.transform.rawModelScale == 2048.0f);
    assert(result.transform.worldModelScale == 2.0f);
    assert((result.transform.basisX == std::array<float, 3>{0.0f, 1.0f, 0.0f}));
    assert((result.transform.basisY == std::array<float, 3>{-1.0f, 0.0f, 0.0f}));
    assert((result.transform.basisZ == std::array<float, 3>{0.0f, 0.0f, 1.0f}));

    // This is the exact FUN_0020def8 column-vector order, not a guessed Euler
    // conversion: 90 degrees around Z maps local +X to world +Y.
    const auto world = ratchet::game::transformRac1LiveMobyRawPositionToWorld(
        result.transform, {3.0f, 4.0f, 5.0f});
    assert((world == std::array<float, 3>{2.0f, 26.0f, 40.0f}));

    const auto origin = ratchet::game::transformRac1LiveMobyRawPositionToWorld(
        result.transform, {0.0f, 0.0f, 0.0f});
    assert(origin == result.transform.position);
}

void testDirectConsumerFormulaWithoutMatrixTransposition() {
    ratchet::game::Rac1LiveMobyWorldTransform transform;
    transform.position = {2.0f, 3.0f, 5.0f};
    transform.rawModelScale = 512.0f;
    transform.worldModelScale =
        transform.rawModelScale * ratchet::game::kRac1LiveMobyRawScaleToWorld;
    transform.basisX = {1.0f, 2.0f, 3.0f};
    transform.basisY = {4.0f, 5.0f, 6.0f};
    transform.basisZ = {7.0f, 8.0f, 9.0f};

    // scaled local = {1,-2,3}; Retail columns give
    // X*1 + Y*(-2) + Z*3 = {14,16,18} before translation.
    const auto world = ratchet::game::transformRac1LiveMobyRawPositionToWorld(
        transform, {2.0f, -4.0f, 6.0f});
    assert((world == std::array<float, 3>{16.0f, 19.0f, 23.0f}));
}

void testExactZeroBasisIsNotMaterialized() {
    Fixture fixture;
    writeVec3(fixture.ram, Fixture::slot(0u) + Layout::kRotationBasisXOffset, {});
    writeVec3(fixture.ram, Fixture::slot(0u) + Layout::kRotationBasisYOffset, {});
    writeVec3(fixture.ram, Fixture::slot(0u) + Layout::kRotationBasisZOffset, {});
    const auto result = inspect(fixture);
    assert(result.status ==
           ratchet::game::Rac1LiveRatchetTransformStatus::RotationBasisNotMaterialized);
    assert((result.transform.position == std::array<float, 3>{10.0f, 20.0f, 30.0f}));
    assert(result.transform.worldModelScale == 2.0f);
}

void testNonFiniteFieldsFailClosed() {
    {
        Fixture fixture;
        writeFloat(fixture.ram,
                   Fixture::slot(0u) + Layout::kWorldPositionOffset,
                   std::numeric_limits<float>::quiet_NaN());
        assert(inspect(fixture).status ==
               ratchet::game::Rac1LiveRatchetTransformStatus::NonFinitePosition);
    }
    {
        Fixture fixture;
        writeFloat(fixture.ram,
                   Fixture::slot(0u) + Layout::kRawModelScaleOffset,
                   std::numeric_limits<float>::infinity());
        assert(inspect(fixture).status ==
               ratchet::game::Rac1LiveRatchetTransformStatus::NonFiniteScale);
    }
    {
        Fixture fixture;
        writeFloat(fixture.ram,
                   Fixture::slot(0u) + Layout::kRotationInputOffset + 4u,
                   std::numeric_limits<float>::quiet_NaN());
        assert(inspect(fixture).status ==
               ratchet::game::Rac1LiveRatchetTransformStatus::NonFiniteRotationInput);
    }
    {
        Fixture fixture;
        writeFloat(fixture.ram,
                   Fixture::slot(0u) + Layout::kRotationBasisYOffset + 8u,
                   std::numeric_limits<float>::quiet_NaN());
        assert(inspect(fixture).status ==
               ratchet::game::Rac1LiveRatchetTransformStatus::NonFiniteRotationBasis);
    }
}

void testCandidateAccountingAndCardinalityFailClosed() {
    Fixture fixture;
    const auto pool = ratchet::game::inspectRac1LiveMobyPool(fixture.ram);
    assert(pool.status == ratchet::game::Rac1LiveMobyPoolStatus::Ok);

    auto accountingMismatch = pool;
    accountingMismatch.ratchetCandidateCount = 0u;
    assert(ratchet::game::inspectRac1LiveRatchetWorldTransform(accountingMismatch).status ==
           ratchet::game::Rac1LiveRatchetTransformStatus::RatchetCandidateAccountingMismatch);

    auto none = pool;
    none.records.at(0).oClass = 1;
    none.ratchetCandidateCount = 0u;
    assert(ratchet::game::inspectRac1LiveRatchetWorldTransform(none).status ==
           ratchet::game::Rac1LiveRatchetTransformStatus::RatchetNotFound);

    auto multiple = pool;
    auto duplicate = multiple.records.at(0);
    duplicate.guestAddress += Layout::kStride;
    duplicate.slotIndex = 1u;
    multiple.records.push_back(duplicate);
    multiple.ratchetCandidateCount = 2u;
    assert(ratchet::game::inspectRac1LiveRatchetWorldTransform(multiple).status ==
           ratchet::game::Rac1LiveRatchetTransformStatus::MultipleRatchets);
}

void testPoolNotReadyIsPreserved() {
    ratchet::game::Rac1LiveMobyPoolSnapshot pool;
    pool.status = ratchet::game::Rac1LiveMobyPoolStatus::PoolNotInitialized;
    assert(ratchet::game::inspectRac1LiveRatchetWorldTransform(pool).status ==
           ratchet::game::Rac1LiveRatchetTransformStatus::PoolNotReady);
}

} // namespace

int main() {
    testRetailWorldFormulaAndBasisColumns();
    testDirectConsumerFormulaWithoutMatrixTransposition();
    testExactZeroBasisIsNotMaterialized();
    testNonFiniteFieldsFailClosed();
    testCandidateAccountingAndCardinalityFailClosed();
    testPoolNotReadyIsPreserved();

    assert(std::string_view(ratchet::game::rac1LiveRatchetTransformStatusName(
               ratchet::game::Rac1LiveRatchetTransformStatus::Ok)) == "ok");
    assert(std::string_view(ratchet::game::rac1LiveRatchetTransformStatusName(
               ratchet::game::Rac1LiveRatchetTransformStatus::RotationBasisNotMaterialized)) ==
           "basis-not-materialized");
    std::cout << "rac1_live_transform_tests: PASS\n";
    return 0;
}
