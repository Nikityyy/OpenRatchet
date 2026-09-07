#include "ps2recomp/control_flow_analyzer.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
using namespace ps2recomp;

Instruction makeRegimmLinkBranch(uint32_t address, uint32_t variant, uint32_t target)
{
    const int64_t deltaBytes = static_cast<int64_t>(target) - static_cast<int64_t>(address + 4u);
    if ((deltaBytes & 3ll) != 0ll)
    {
        throw std::runtime_error("REGIMM target is not word aligned");
    }

    const int64_t offsetWords = deltaBytes / 4ll;
    if (offsetWords < INT16_MIN || offsetWords > INT16_MAX)
    {
        throw std::runtime_error("REGIMM target is outside the signed 16-bit branch range");
    }

    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_REGIMM;
    inst.rs = 8u;
    inst.rt = variant;
    inst.immediate = static_cast<uint16_t>(static_cast<int16_t>(offsetWords));
    inst.simmediate = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(inst.immediate)));
    inst.isBranch = true;
    inst.isCall = true;
    inst.hasDelaySlot = true;
    return inst;
}

Instruction makeNop(uint32_t address)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_ADDIU;
    inst.rt = 0u;
    return inst;
}

bool provesResume(
    uint32_t ownerStart,
    uint32_t ownerEnd,
    uint32_t branchPc,
    uint32_t targetPc,
    uint32_t variant)
{
    const uint32_t resumePc = branchPc + 8u;
    const std::vector<Section> sections{
        {".text", ownerStart, ownerEnd - ownerStart, 0u, true, false, false, true, nullptr}};
    const std::unordered_map<uint32_t, std::vector<uint32_t>> jumpTables{};
    const ControlFlowAnalyzer analyzer(sections, jumpTables, nullptr);

    Function function{};
    function.name = "regimm_link_owner";
    function.start = ownerStart;
    function.end = ownerEnd;
    function.isRecompiled = true;

    const std::vector<Instruction> instructions{
        makeRegimmLinkBranch(branchPc, variant, targetPc),
        makeNop(branchPc + 4u),
        makeNop(resumePc),
        makeNop(targetPc)};

    const ControlFlowAnalysisResult result = analyzer.analyze(function, instructions, nullptr);
    return result.entryPoints.contains(targetPc) &&
           result.entryPoints.contains(resumePc) &&
           result.resumeEntryPoints.contains(resumePc);
}
} // namespace

int main()
{
    struct RetailSite
    {
        uint32_t ownerStart;
        uint32_t ownerEnd;
        uint32_t branchPc;
        uint32_t targetPc;
        uint32_t resumePc;
    };

    // These are the four branch-and-link sites present in the current R&C1
    // generated fallback.  All are BGEZAL and therefore can return through PC+8.
    constexpr std::array<RetailSite, 4> kRetailSites{{
        {0x0022BBA0u, 0x0022BF94u, 0x0022BE00u, 0x0022BEC4u, 0x0022BE08u},
        {0x00217C18u, 0x00218888u, 0x0021835Cu, 0x0021880Cu, 0x00218364u},
        {0x00217C18u, 0x00218888u, 0x00218484u, 0x0021880Cu, 0x0021848Cu},
        {0x00217C18u, 0x00218888u, 0x0021863Cu, 0x0021880Cu, 0x00218644u},
    }};

    for (const RetailSite &site : kRetailSites)
    {
        if (site.resumePc != site.branchPc + 8u ||
            !provesResume(site.ownerStart, site.ownerEnd, site.branchPc, site.targetPc, REGIMM_BGEZAL))
        {
            std::cerr << "Missing resumable BGEZAL return PC at 0x" << std::hex << site.resumePc << '\n';
            return 1;
        }
    }

    // Keep the regression general: all R5900 REGIMM branch-and-link variants
    // have the same PC+8 return-continuation requirement when their target returns.
    constexpr std::array<uint32_t, 4> kLinkVariants{
        REGIMM_BLTZAL,
        REGIMM_BGEZAL,
        REGIMM_BLTZALL,
        REGIMM_BGEZALL,
    };
    for (const uint32_t variant : kLinkVariants)
    {
        if (!provesResume(0x2200u, 0x2220u, 0x2200u, 0x2210u, variant))
        {
            std::cerr << "Missing generic REGIMM link resume for variant 0x" << std::hex << variant << '\n';
            return 1;
        }
    }

    std::cout << "PS2Recomp REGIMM branch-and-link resume regression passed\n";
    return 0;
}
