#include "game/rac1_live_transform.h"

#include <cmath>

namespace ratchet::game {
namespace {

bool finiteVector(const std::array<float, 3>& value) noexcept {
    return std::isfinite(value[0]) &&
           std::isfinite(value[1]) &&
           std::isfinite(value[2]);
}

bool exactlyZeroVector(const std::array<float, 3>& value) noexcept {
    return value[0] == 0.0f && value[1] == 0.0f && value[2] == 0.0f;
}

} // namespace

Rac1LiveRatchetTransformResult inspectRac1LiveRatchetWorldTransform(
    const Rac1LiveMobyPoolSnapshot& poolSnapshot) {
    Rac1LiveRatchetTransformResult out;
    if (poolSnapshot.status != Rac1LiveMobyPoolStatus::Ok) {
        out.status = Rac1LiveRatchetTransformStatus::PoolNotReady;
        return out;
    }

    const Rac1LiveMobyRecord* ratchet = nullptr;
    for (const Rac1LiveMobyRecord& record : poolSnapshot.records) {
        if (!record.participatesInRetailTraversal || record.oClass != 0) {
            continue;
        }
        ++out.ratchetCandidates;
        if (ratchet == nullptr) {
            ratchet = &record;
        }
    }

    if (out.ratchetCandidates != poolSnapshot.ratchetCandidateCount) {
        out.status = Rac1LiveRatchetTransformStatus::RatchetCandidateAccountingMismatch;
        return out;
    }
    if (out.ratchetCandidates == 0u) {
        out.status = Rac1LiveRatchetTransformStatus::RatchetNotFound;
        return out;
    }
    if (out.ratchetCandidates != 1u || ratchet == nullptr) {
        out.status = Rac1LiveRatchetTransformStatus::MultipleRatchets;
        return out;
    }

    const Rac1LiveMobyWorldTransformState& retail = ratchet->worldTransform;
    out.transform.mobyGuestAddress = ratchet->guestAddress;
    out.transform.oClass = ratchet->oClass;
    out.transform.position = retail.position;
    out.transform.rawModelScale = retail.rawModelScale;
    out.transform.worldModelScale = retail.rawModelScale * kRac1LiveMobyRawScaleToWorld;
    out.transform.rotationInput = retail.rotationInput;
    out.transform.basisX = retail.basisX;
    out.transform.basisY = retail.basisY;
    out.transform.basisZ = retail.basisZ;

    if (!finiteVector(out.transform.position)) {
        out.status = Rac1LiveRatchetTransformStatus::NonFinitePosition;
        return out;
    }
    if (!std::isfinite(out.transform.rawModelScale) ||
        !std::isfinite(out.transform.worldModelScale)) {
        out.status = Rac1LiveRatchetTransformStatus::NonFiniteScale;
        return out;
    }
    if (!finiteVector(out.transform.rotationInput)) {
        out.status = Rac1LiveRatchetTransformStatus::NonFiniteRotationInput;
        return out;
    }

    // FUN_0020c5f0 zeroes the entire Moby before initialization. The only
    // proved writer of all three cached basis vectors is FUN_0020def8. Hence
    // three exact zero xyz vectors are an objective pre-materialization state,
    // not a reason to derive a guessed host matrix from +0x40.
    if (exactlyZeroVector(out.transform.basisX) &&
        exactlyZeroVector(out.transform.basisY) &&
        exactlyZeroVector(out.transform.basisZ)) {
        out.status = Rac1LiveRatchetTransformStatus::RotationBasisNotMaterialized;
        return out;
    }

    if (!finiteVector(out.transform.basisX) ||
        !finiteVector(out.transform.basisY) ||
        !finiteVector(out.transform.basisZ)) {
        out.status = Rac1LiveRatchetTransformStatus::NonFiniteRotationBasis;
        return out;
    }

    out.status = Rac1LiveRatchetTransformStatus::Ok;
    return out;
}

std::array<float, 3> transformRac1LiveMobyRawPositionToWorld(
    const Rac1LiveMobyWorldTransform& transform,
    const std::array<float, 3>& rawPosition) noexcept {
    const float x = rawPosition[0] * transform.worldModelScale;
    const float y = rawPosition[1] * transform.worldModelScale;
    const float z = rawPosition[2] * transform.worldModelScale;

    return {
        transform.position[0] +
            transform.basisX[0] * x + transform.basisY[0] * y + transform.basisZ[0] * z,
        transform.position[1] +
            transform.basisX[1] * x + transform.basisY[1] * y + transform.basisZ[1] * z,
        transform.position[2] +
            transform.basisX[2] * x + transform.basisY[2] * y + transform.basisZ[2] * z,
    };
}

const char* rac1LiveRatchetTransformStatusName(
    Rac1LiveRatchetTransformStatus status) noexcept {
    switch (status) {
    case Rac1LiveRatchetTransformStatus::Ok:
        return "ok";
    case Rac1LiveRatchetTransformStatus::PoolNotReady:
        return "pool-not-ready";
    case Rac1LiveRatchetTransformStatus::RatchetCandidateAccountingMismatch:
        return "ratchet-candidate-accounting-mismatch";
    case Rac1LiveRatchetTransformStatus::RatchetNotFound:
        return "ratchet-not-found";
    case Rac1LiveRatchetTransformStatus::MultipleRatchets:
        return "multiple-ratchets";
    case Rac1LiveRatchetTransformStatus::NonFinitePosition:
        return "non-finite-position";
    case Rac1LiveRatchetTransformStatus::NonFiniteScale:
        return "non-finite-scale";
    case Rac1LiveRatchetTransformStatus::NonFiniteRotationInput:
        return "non-finite-rotation-input";
    case Rac1LiveRatchetTransformStatus::RotationBasisNotMaterialized:
        return "basis-not-materialized";
    case Rac1LiveRatchetTransformStatus::NonFiniteRotationBasis:
        return "non-finite-rotation-basis";
    }
    return "unknown";
}

} // namespace ratchet::game
