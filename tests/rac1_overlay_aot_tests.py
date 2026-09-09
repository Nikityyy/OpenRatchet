from __future__ import annotations

import json
import os
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import rac1_overlay_aot as aot


class OverlayAotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.toc_path = REPO_ROOT / "build" / "toc.json"
        cls.wad_path = REPO_ROOT / "build" / "extracted" / "levels" / "level_00.wad"
        cls.wad158_path = REPO_ROOT / "build" / "extracted" / "wads" / "wad_158.wad"
        cls.have_retail = cls.toc_path.is_file() and cls.wad_path.is_file()
        cls.have_wad158 = cls.wad158_path.is_file()

    def retail_image(self) -> aot.OverlayImage:
        if not self.have_retail:
            self.skipTest("Retail Level-0 extraction is not available")
        location = aot.load_level_location(self.toc_path, 0)
        return aot.parse_level_overlay(self.wad_path.read_bytes(), location)


    def wad158_image(self) -> aot.OverlayImage:
        if not self.have_wad158:
            self.skipTest("Retail wad_158 extraction is not available")
        return aot.parse_raw_overlay_container(self.wad158_path.read_bytes(), 158)

    def test_wad158_overlay_exact_structure(self) -> None:
        image = self.wad158_image()
        self.assertEqual(image.generation_entry, 0x001E9658)
        self.assertEqual(len(image.segments), 7)
        self.assertEqual(image.payload_bytes, 908_304)
        self.assertEqual(image.container_size, 0xDDC80)
        self.assertEqual(
            [(segment.destination, segment.payload_size, segment.field8) for segment in image.segments],
            [
                (0x0015EF00, 0x2328, 1),
                (0x00161280, 0x41B0, 8),
                (0x00165480, 0x836F8, 1),
                (0x001E8B80, 0x0C, 1),
                (0x001E8C00, 0x14, 1),
                (0x001E8C80, 0x08, 1),
                (0x001E8D00, 0x54018, 1),
            ],
        )
        executable = image.executable_segment
        self.assertEqual((executable.destination, executable.end), (0x001E8D00, 0x0023CD18))
        self.assertEqual(
            aot.count_direct_overlay_targets(image),
            {"j_inside": 200, "jal_inside": 3307, "direct_external": 568},
        )
        self.assertEqual(len(aot.direct_overlay_targets(image)["jal_targets"]), 684)

    def test_wad158_raw_parser_rejects_nonzero_sector_padding(self) -> None:
        if not self.have_wad158:
            self.skipTest("Retail wad_158 extraction is not available")
        data = bytearray(self.wad158_path.read_bytes())
        data[-1] = 1
        with self.assertRaisesRegex(aot.OverlayAotError, "non-zero trailing data"):
            aot.parse_raw_overlay_container(bytes(data), 158)

    def test_wad158_synthetic_elf_preserves_every_payload(self) -> None:
        image = self.wad158_image()
        elf = aot.build_overlay_elf(image)
        parsed = aot.parse_elf_program_headers(elf)
        self.assertEqual(parsed["entry"], 0x001E9658)
        self.assertEqual(parsed["phnum"], 7)
        for segment, header in zip(image.segments, parsed["program_headers"], strict=True):
            self.assertEqual(header["vaddr"], segment.destination)
            self.assertEqual(elf[header["offset"]:header["offset"] + header["filesz"]], segment.payload)

    def test_repo_owned_headless_ghidra_exporter_has_required_contract(self) -> None:
        exporter = REPO_ROOT / "tools" / "ghidra" / "ExportOpenRatchetFunctions.java"
        self.assertTrue(exporter.is_file())
        text = exporter.read_text(encoding="utf-8")
        self.assertIn('writer.write("Name,Start,End,Size\\n")', text)
        self.assertIn("parseExecutableRanges(args)", text)
        self.assertIn("overlayExecutable", text)
        self.assertIn("block.isExecute()", text)
        self.assertIn("block.isLoaded()", text)
        self.assertNotIn("memory.getExecuteSet().intersect", text)
        self.assertNotIn("getLoadedAndInitializedAddressSet()", text)
        self.assertIn("body.getMaxAddress()", text)
        self.assertIn("getReferencesTo(address)", text)
        self.assertIn("isCall()", text)
        self.assertIn("CreateFunctionCmd.getFunctionBody", text)
        self.assertIn("parseRequiredCallableEntries(args)", text)
        self.assertIn("entry0x", text)
        self.assertIn('OPENRATCHET_EXPORTER_PROTOCOL = "openratchet-entry0x-v1"', text)
        self.assertIn("OpenRatchet exporter protocol:", text)
        self.assertIn('token.startsWith("entry\\\\@")', text)
        self.assertIn('token.equals("--entry")', text)
        self.assertIn('token.startsWith("--entry=")', text)
        self.assertIn("required Retail callback entries", text)
        self.assertIn("overlayExecutable.contains(body)", text)
        self.assertIn("overlayExecutable.getRangeContaining(address)", text)
        self.assertIn("(raw >>> 26) != 0x03", text)
        self.assertIn("WAD-direct JAL entries", text)
        self.assertNotIn("askFile(", text)
        self.assertNotIn("ExportPS2Functions.java", text)

    def test_ghidra_exporter_staging_is_content_addressed_and_cache_safe(self) -> None:
        exporter = REPO_ROOT / "tools" / "ghidra" / "ExportOpenRatchetFunctions.java"
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            staged = aot.stage_ghidra_export_script(exporter, work)
            self.assertEqual(staged.parent, work / "ghidra-scripts")
            self.assertRegex(
                staged.name, r"^ExportOpenRatchetFunctions_[0-9a-f]{16}\.java$"
            )
            class_name = staged.stem
            staged_text = staged.read_text(encoding="utf-8")
            self.assertIn(f"public class {class_name} extends GhidraScript", staged_text)
            self.assertNotIn(
                "public class ExportOpenRatchetFunctions extends GhidraScript", staged_text
            )
            self.assertEqual(aot.stage_ghidra_export_script(exporter, work), staged)

    def test_ghidra_exporter_staging_rejects_stale_protocol(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            stale = work / "ExportOpenRatchetFunctions.java"
            stale.write_text(
                "public class ExportOpenRatchetFunctions extends GhidraScript {}\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(aot.OverlayAotError, "stale or incompatible"):
                aot.stage_ghidra_export_script(stale, work / "out")

    def test_build_local_dispatch_table_does_not_touch_equal_content(self) -> None:
        source_text = (
            "g_ps2RecompiledFunctionTableBase = 0x00100000u;\n"
            "g_ps2RecompiledFunctionTableEnd = 0x00101000u;\n"
            "g_ps2RecompiledFunctionTableSlotCount = 1024u;\n"
            "void* g_ps2RecompiledFunctionTable[1024u] = {};\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "register_functions.cpp"
            output = root / "build" / "register_functions.cpp"
            source.write_text(source_text, encoding="utf-8")
            aot.write_expanded_boot_function_table(source, output, 0x00101000)
            expected = output.read_text(encoding="utf-8")
            sentinel_ns = 1_700_000_000_123_456_700
            os.utime(output, ns=(sentinel_ns, sentinel_ns))

            aot.write_expanded_boot_function_table(source, output, 0x00101000)

            self.assertEqual(output.read_text(encoding="utf-8"), expected)
            self.assertEqual(output.stat().st_mtime_ns, sentinel_ns)

    def test_ghidra_scan_range_argument_is_end_exclusive_and_overlay_local(self) -> None:
        image = aot.OverlayImage(
            level_id=999,
            generation_entry=0x00101010,
            segments=(
                aot.OverlaySegment(0x00100000, 0x1000, 1, 0x00101010, 0, b"\0" * 0x1000),
                aot.OverlaySegment(0x00101000, 0x234, 1, 0x00101010, 0x1010, b"\0" * 0x234),
            ),
            container_size=0x2000,
        )
        self.assertEqual(
            aot.ghidra_executable_range_args(image),
            ["0x00101000:0x00101234"],
        )

    def test_ghidra_scan_ranges_are_exact_overlay_executable_pt_loads(self) -> None:
        wad158 = self.wad158_image()
        level0 = self.retail_image()
        self.assertEqual(
            aot.ghidra_executable_range_args(wad158),
            ["0x001e8d00:0x0023cd18"],
        )
        self.assertEqual(
            aot.ghidra_executable_range_args(level0),
            ["0x001eaa00:0x002f0cd0"],
        )

    def test_ghidra_export_cli_always_carries_explicit_executable_range(self) -> None:
        image = self.wad158_image()
        csv_path = Path(r"C:\\tmp\\ghidra.csv")
        self.assertEqual(
            aot.ghidra_export_script_args(image, csv_path),
            [str(csv_path), "0x001e8d00:0x0023cd18"],
        )
        self.assertEqual(aot.ghidra_required_callable_entry_args(image), [])

    def test_level0_overlay_exact_structure(self) -> None:
        image = self.retail_image()
        self.assertEqual(image.generation_entry, 0x00245C28)
        self.assertEqual(len(image.segments), 7)
        self.assertEqual(image.payload_bytes, 1_645_532)
        self.assertEqual(image.container_size, 1_645_644)
        self.assertEqual(
            [(segment.destination, segment.payload_size, segment.field8) for segment in image.segments],
            [
                (0x0015EF00, 0x2EF0, 1),
                (0x00161E00, 0x41B0, 8),
                (0x00166000, 0x842B0, 1),
                (0x001EA300, 0x504, 1),
                (0x001EA880, 0xA0, 1),
                (0x001EA980, 0x18, 1),
                (0x001EAA00, 0x1062D0, 1),
            ],
        )
        executable = image.executable_segment
        self.assertEqual((executable.destination, executable.end), (0x001EAA00, 0x002F0CD0))
        self.assertEqual(
            aot.count_direct_overlay_targets(image),
            {"j_inside": 248, "jal_inside": 17_292, "direct_external": 752},
        )

    def test_level0_registry_callback_table_exact_structure(self) -> None:
        image = self.retail_image()
        callbacks = aot.level0_registry_callback_targets(image)
        self.assertEqual(len(callbacks), 75)
        self.assertIn(0x002A4CD8, callbacks)
        self.assertIn(0x002A57A8, callbacks)
        self.assertIn(0x002A5DD8, callbacks)
        self.assertIn(0x002E3D88, callbacks)

        args = aot.ghidra_required_callable_entry_args(image)
        self.assertEqual(len(args), 75)
        self.assertIn("entry0x002a57a8", args)
        self.assertIn("entry0x002a5dd8", args)

    def test_level0_registry_callback_2a57a8_is_real_function_boundary(self) -> None:
        image = self.retail_image()
        executable = image.executable_segment

        def word(pc: int) -> int:
            offset = pc - executable.destination
            return struct.unpack_from("<I", executable.payload, offset)[0]

        callbacks = aot.level0_registry_callback_targets(image)
        self.assertIn(0x002A57A8, callbacks)
        self.assertIn(0x002A5DD8, callbacks)
        # The problematic callback begins immediately after the preceding function's
        # jr-ra + delay slot, and it ends with its own jr-ra + delay slot immediately
        # before the next consumer-proved callback.  Ghidra may miss the Function
        # object, but Retail proves the entry point and the code proves it is sane.
        self.assertEqual(word(0x002A57A0), 0x03E00008)
        self.assertEqual(word(0x002A5DD0), 0x03E00008)
        self.assertEqual(0x002A5DD8, 0x002A5DD0 + 8)

    def test_level0_ghidra_cli_carries_all_registry_callbacks(self) -> None:
        image = self.retail_image()
        csv_path = Path(r"C:\\tmp\\level0-ghidra.csv")
        args = aot.ghidra_export_script_args(image, csv_path)
        self.assertEqual(args[0], str(csv_path))
        self.assertEqual(args[1], "0x001eaa00:0x002f0cd0")
        entry_args = args[2:]
        self.assertEqual(len(entry_args), 75)
        self.assertIn("entry0x002a57a8", entry_args)
        self.assertEqual(len(entry_args), len(set(entry_args)))
        self.assertTrue(all(arg.startswith("entry0x") for arg in entry_args))
        self.assertTrue(all(arg.isalnum() for arg in entry_args))
        self.assertTrue(all(not arg.startswith("-") for arg in entry_args))

    def test_synthetic_elf_preserves_every_overlay_payload(self) -> None:
        image = self.retail_image()
        elf = aot.build_overlay_elf(image)
        parsed = aot.parse_elf_program_headers(elf)
        self.assertEqual(parsed["entry"], 0x00245C28)
        self.assertEqual(parsed["phnum"], 7)
        headers = parsed["program_headers"]
        for segment, header in zip(image.segments, headers, strict=True):
            self.assertEqual(header["vaddr"], segment.destination)
            self.assertEqual(header["filesz"], segment.payload_size)
            self.assertEqual(elf[header["offset"]:header["offset"] + header["filesz"]], segment.payload)
        self.assertEqual(headers[-1]["flags"], aot.PF_R | aot.PF_X)
        self.assertTrue(all(header["flags"] == aot.PF_R | aot.PF_W for header in headers[:-1]))
        self.assertEqual(parsed["shnum"], 0)

    def test_oracle_proves_executable_generation_matches_wad(self) -> None:
        oracle = Path("/mnt/data/oracle/baseline/ee_ram.bin")
        if not oracle.is_file():
            self.skipTest("Retail oracle RAM is not mounted")
        image = self.retail_image()
        result = aot.validate_oracle_ram(image, oracle.read_bytes())
        self.assertTrue(result["executable_match"])
        # Runtime mutates some data segments after load; code itself must remain exact.
        matches = [segment["match"] for segment in result["segments"]]
        self.assertEqual(matches, [False, False, False, True, True, True, True])

    def test_prepare_writes_deterministic_ps2recomp_input(self) -> None:
        if not self.have_retail:
            self.skipTest("Retail Level-0 extraction is not available")
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "overlay"
            manifest = aot.prepare_overlay_aot(self.toc_path, self.wad_path, 0, out)
            self.assertEqual(manifest["generation_entry"], "0x00245c28")
            self.assertEqual(manifest["segment_count"], 7)
            self.assertTrue((out / "level_00_overlay.elf").is_file())
            config = (out / "ps2recomp.toml").read_text(encoding="utf-8")
            self.assertIn('ghidra_output = ""', config)
            self.assertIn("level_00_overlay.elf", config)
            disk_manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(disk_manifest["elf_sha256"], manifest["elf_sha256"])


    def _write_complete_direct_target_csv(
        self, image: aot.OverlayImage, path: Path, *, covering_body: bool = False
    ) -> list[int]:
        targets = sorted(set(aot.direct_overlay_targets(image)["jal_targets"]) | {image.generation_entry})
        with path.open("w", encoding="utf-8", newline="") as stream:
            stream.write("Name,Start,End,Size\n")
            if covering_body:
                executable = image.executable_segment
                stream.write(
                    f"overlay_body,0x{executable.destination:08X},"
                    f"0x{executable.end:08X},{executable.payload_size}\n"
                )
            for pc in targets:
                stream.write(f"FUN_{pc:08x},0x{pc:08X},0x{pc + 4:08X},4\n")
        return targets

    def test_ghidra_map_must_cover_generation_entry_and_every_direct_jal_target(self) -> None:
        image = self.retail_image()
        with tempfile.TemporaryDirectory() as td:
            csv_path = Path(td) / "ghidra.csv"
            targets = self._write_complete_direct_target_csv(image, csv_path)
            result = aot.validate_ghidra_map(image, csv_path)
            self.assertEqual(result["records"], len(targets))
            self.assertEqual(result["unique_direct_jal_targets"], 1516)
            self.assertEqual(result["missing_direct_jal_targets"], 0)

            # Delete one provably direct callable target; the build must fail closed.
            victim = next(pc for pc in targets if pc != image.generation_entry)
            lines = csv_path.read_text(encoding="utf-8").splitlines()
            csv_path.write_text(
                "\n".join(line for line in lines if f"0x{victim:08X}," not in line) + "\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(aot.OverlayAotError, "missed 1 direct in-overlay JAL target"):
                aot.validate_ghidra_map(image, csv_path)

    def test_wad158_direct_jal_targets_absorbed_by_ghidra_body_are_augmented(self) -> None:
        image = self.wad158_image()
        missing = {0x001F7950, 0x001F7A10, 0x00213BB0}
        direct = set(aot.direct_overlay_targets(image)["jal_targets"])
        self.assertTrue(missing <= direct)
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / "ghidra.csv"
            output = root / "ghidra-aot.csv"
            starts = direct | {image.generation_entry}
            records = []
            occupied_starts = starts - missing
            for pc in sorted(occupied_starts):
                records.append((f"FUN_{pc:08x}", pc, pc + 4, 4))
            executable = image.executable_segment
            for target in sorted(missing):
                owner_start = target - 4
                while owner_start in occupied_starts or owner_start in missing:
                    owner_start -= 4
                self.assertGreaterEqual(owner_start, executable.destination)
                records.append((f"owner_{target:08x}", owner_start, target + 8, target + 8 - owner_start))
            records.sort(key=lambda item: item[1])
            with source.open("w", encoding="utf-8", newline="") as stream:
                stream.write("Name,Start,End,Size\n")
                for name, start, end, size in records:
                    stream.write(f"{name},0x{start:08X},0x{end:08X},{size}\n")

            raw = aot.validate_ghidra_map(
                image, source, require_direct_jal_targets=False
            )
            self.assertEqual(raw["missing_direct_jal_targets"], 3)
            summary = aot.augment_ghidra_map_with_required_entries(image, source, output)
            self.assertEqual(summary["synthetic_direct_jal_entries"], 3)
            self.assertEqual(summary["synthetic_registry_entries"], 0)
            final = aot.validate_ghidra_map(image, output)
            self.assertEqual(final["missing_direct_jal_targets"], 0)
            final_starts = {record.start for record in aot.read_ghidra_csv(output)}
            self.assertTrue(missing <= final_starts)

    def test_augmentation_refuses_direct_jal_target_without_ghidra_body(self) -> None:
        image = self.wad158_image()
        direct = sorted(set(aot.direct_overlay_targets(image)["jal_targets"]) | {image.generation_entry})
        victim = 0x001F7950
        self.assertIn(victim, direct)
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / "ghidra.csv"
            output = root / "ghidra-aot.csv"
            with source.open("w", encoding="utf-8", newline="") as stream:
                stream.write("Name,Start,End,Size\n")
                for pc in direct:
                    if pc == victim:
                        continue
                    stream.write(f"FUN_{pc:08x},0x{pc:08X},0x{pc + 4:08X},4\n")
            with self.assertRaisesRegex(
                aot.OverlayAotError,
                r"direct JAL target 0x001f7950 is not contained in any Ghidra function",
            ):
                aot.augment_ghidra_map_with_required_entries(image, source, output)

    def test_ps2recomp_output_must_register_every_ghidra_entry(self) -> None:
        image = self.retail_image()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            csv_path = root / "ghidra.csv"
            self._write_complete_direct_target_csv(image, csv_path, covering_body=True)
            aot_csv = root / "ghidra-aot.csv"
            augmentation = aot.augment_ghidra_map_with_required_entries(image, csv_path, aot_csv)
            self.assertEqual(augmentation["required_registry_entries"], 75)
            starts = sorted({record.start for record in aot.read_ghidra_csv(aot_csv)})
            raw = root / "raw"
            raw.mkdir()
            register = raw / "register_functions.cpp"
            register.write_text(
                "\n".join(
                    f"g_ps2RecompiledFunctionTable[{index}] = f_{pc:08x}; // 0x{pc:08x}"
                    for index, pc in enumerate(starts)
                ) + "\n",
                encoding="utf-8",
            )
            result = aot.verify_ps2recomp_output(image, aot_csv, raw)
            self.assertEqual(result["ghidra_records"], len(starts))
            self.assertEqual(result["registered_entries"], len(starts))
            self.assertEqual(result["registry_callback_entries"], 75)
            self.assertEqual(result["missing_ghidra_entries"], 0)
            self.assertEqual(result["missing_registry_entries"], 0)

            register.write_text(
                "\n".join(
                    f"g_ps2RecompiledFunctionTable[{index}] = f_{pc:08x}; // 0x{pc:08x}"
                    for index, pc in enumerate(starts[1:])
                ) + "\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(aot.OverlayAotError, "omitted 1 Ghidra entry point"):
                aot.verify_ps2recomp_output(image, aot_csv, raw)

    def test_controlled_ps2recomp_config_uses_ghidra_without_overlay_local_stubs(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            elf = root / "overlay.elf"
            output = root / "raw"
            csv_path = root / "ghidra.csv"
            elf.write_bytes(b"elf")
            csv_path.write_text("Name,Start,End,Size\n", encoding="utf-8")
            config = root / "overlay.toml"
            aot.write_ps2recomp_config(config, elf, output, csv_path)
            text = config.read_text(encoding="utf-8")
            self.assertIn(f'ghidra_output = "{csv_path.resolve().as_posix()}"', text)
            self.assertIn("low_memory_mode = true", text)
            self.assertIn("stubs = []", text)
            self.assertIn("skip = []", text)

    def test_finalize_namespaces_all_generated_symbols_and_emits_secondary_table(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            raw = Path(td) / "raw"
            out = Path(td) / "final"
            raw.mkdir()
            (raw / "ps2_recompiled_functions.h").write_text(
                "#ifndef PS2_RECOMPILED_FUNCTIONS_H\n"
                "#define PS2_RECOMPILED_FUNCTIONS_H\n"
                "#include <cstdint>\n"
                "struct R5900Context; class PS2Runtime;\n"
                "void sub_00245C28(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);\n"
                "void sub_00246330(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);\n"
                "#endif\n",
                encoding="utf-8",
            )
            (raw / "ps2_recompiled_stubs.h").write_text("#pragma once\n", encoding="utf-8")
            (raw / "a.cpp").write_text(
                '#include "ps2_recompiled_functions.h"\n'
                "void sub_00245C28(uint8_t*, R5900Context*, PS2Runtime*) { (void)&sub_00246330; }\n"
                "void sub_00246330(uint8_t*, R5900Context*, PS2Runtime*) {}\n",
                encoding="utf-8",
            )
            (raw / "register_functions.cpp").write_text(
                "g_ps2RecompiledFunctionTable[0] = sub_00245C28; // 0x245c28\n"
                "g_ps2RecompiledFunctionTable[1] = sub_00246330; // 0x246330\n",
                encoding="utf-8",
            )
            summary = aot.finalize_overlay_module(
                raw,
                out,
                "rac1_l0_",
                "ratchet::generated::level0",
                0x245C28,
                ((0x245C20, 0x246340),),
            )
            self.assertEqual(summary["function_symbols"], 2)
            self.assertEqual(summary["registration_entries"], 2)
            self.assertEqual(summary["materialized_ranges"], 1)
            self.assertEqual(summary["materialized_slots"], (0x246340 - 0x245C20) // 4)
            source = (out / "a.cpp").read_text(encoding="utf-8")
            self.assertIn("rac1_l0_sub_00245C28", source)
            self.assertIn("rac1_l0_sub_00246330", source)
            self.assertNotIn("void sub_00245C28", source)
            header = (out / "ps2_recompiled_functions.h").read_text(encoding="utf-8")
            self.assertIn("OPENRATCHET_RATCHET__GENERATED__LEVEL0_PS2_RECOMPILED_FUNCTIONS_H", header)
            table_header = (out / "openratchet_overlay_table.h").read_text(encoding="utf-8")
            self.assertIn("kMaterializedRanges", table_header)
            table = (out / "openratchet_overlay_table.cpp").read_text(encoding="utf-8")
            self.assertIn("{0x00245c20u, 0x00246340u}", table)
            self.assertIn("{0x00245c28u, &rac1_l0_sub_00245C28}", table)
            self.assertFalse((out / "register_functions.cpp").exists())

    def test_finalize_rejects_callable_entries_outside_materialized_ranges(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            raw = Path(td) / "raw"
            out = Path(td) / "final"
            raw.mkdir()
            (raw / "ps2_recompiled_functions.h").write_text(
                "#pragma once\n"
                "#include <cstdint>\n"
                "struct R5900Context; class PS2Runtime;\n"
                "void sub_00245C28(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);\n"
                "void sub_00246330(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);\n",
                encoding="utf-8",
            )
            (raw / "a.cpp").write_text(
                '#include "ps2_recompiled_functions.h"\n'
                "void sub_00245C28(uint8_t*, R5900Context*, PS2Runtime*) {}\n"
                "void sub_00246330(uint8_t*, R5900Context*, PS2Runtime*) {}\n",
                encoding="utf-8",
            )
            (raw / "register_functions.cpp").write_text(
                "g_ps2RecompiledFunctionTable[0] = sub_00245C28; // 0x245c28\n"
                "g_ps2RecompiledFunctionTable[1] = sub_00246330; // 0x246330\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(aot.OverlayAotError, "outside materialized ranges"):
                aot.finalize_overlay_module(
                    raw, out, "rac1_l0_", "ratchet::generated::level0",
                    0x245C28, ((0x245C20, 0x245C30),),
                )

    def test_v7_metadata_upgrade_is_fail_closed_on_semantic_input_changes(self) -> None:
        prior = {
            "generation": "level_00",
            "source": "source-hash",
            "tool": aot.V7_METADATA_UPGRADE_SOURCE_TOOL_SHA256,
            "patch": "patch-hash",
            "exporter": "exporter-hash",
            "ghidra": {"version": "same"},
            "ps2recomp_revision": "revision",
            "toc": "toc-hash",
            "schema": "rac1-overlay-aot-v7",
        }
        current = dict(prior)
        current["tool"] = "new-tool"
        current["schema"] = "rac1-overlay-aot-v8"
        self.assertTrue(aot._v7_metadata_upgrade_compatible(prior, current))
        unknown_v7_tool = dict(prior)
        unknown_v7_tool["tool"] = "different-v7-tool"
        self.assertFalse(aot._v7_metadata_upgrade_compatible(unknown_v7_tool, current))
        changed_source = dict(current)
        changed_source["source"] = "different-source"
        self.assertFalse(aot._v7_metadata_upgrade_compatible(prior, changed_source))
        missing_toc = dict(current)
        del missing_toc["toc"]
        self.assertFalse(aot._v7_metadata_upgrade_compatible(prior, missing_toc))
        wrong_schema = dict(current)
        wrong_schema["schema"] = "rac1-overlay-aot-v9"
        self.assertFalse(aot._v7_metadata_upgrade_compatible(prior, wrong_schema))

    def test_v7_metadata_upgrade_adds_ranges_without_rewriting_function_bodies(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            out = Path(td)
            header = out / "openratchet_overlay_table.h"
            source = out / "openratchet_overlay_table.cpp"
            body = out / "FUN_body.cpp"
            header.write_text(
                "#pragma once\n"
                "#include <cstddef>\n"
                "#include \"game/rac1_overlay_aot_dispatch.h\"\n"
                "namespace ratchet::generated::level0 {\n"
                "using FunctionEntry = ratchet::game::Rac1OverlayFunctionEntry;\n"
                "extern const FunctionEntry kFunctions[];\n"
                "}\n",
                encoding="utf-8",
            )
            source.write_text(
                '#include "openratchet_overlay_table.h"\n'
                "namespace ratchet::generated::level0 {\n"
                "const FunctionEntry kFunctions[] = {};\n"
                "}\n",
                encoding="utf-8",
            )
            body.write_text("unchanged-body\n", encoding="utf-8")
            before = body.read_bytes()
            aot._upgrade_overlay_materialized_range_metadata(
                out, "ratchet::generated::level0",
                ((0x15EF00, 0x161DF0), (0x1EAA00, 0x2F0CD0)),
            )
            self.assertEqual(body.read_bytes(), before)
            upgraded_header = header.read_text(encoding="utf-8")
            upgraded_source = source.read_text(encoding="utf-8")
            self.assertIn("kMaterializedRangeCount", upgraded_header)
            self.assertIn("{0x0015ef00u, 0x00161df0u}", upgraded_source)
            self.assertIn("{0x001eaa00u, 0x002f0cd0u}", upgraded_source)
            # Idempotent rewrite: a second migration must not duplicate metadata.
            aot._upgrade_overlay_materialized_range_metadata(
                out, "ratchet::generated::level0",
                ((0x15EF00, 0x161DF0), (0x1EAA00, 0x2F0CD0)),
            )
            self.assertEqual(
                source.read_text(encoding="utf-8").count("BEGIN OpenRatchet materialized ranges"), 1
            )

    def test_symbol_rewriter_uses_one_compiled_pass_without_partial_identifier_matches(self) -> None:
        mapping = {
            "sub_00100000": "rac1_w158_sub_00100000",
            "sub_00100000_extra": "rac1_w158_sub_00100000_extra",
            "FUN_00100020": "rac1_w158_FUN_00100020",
        }
        pattern = aot._compile_symbol_rewriter(mapping)
        text = (
            "sub_00100000 sub_00100000_extra FUN_00100020 "
            "prefix_sub_00100000 sub_00100000_suffix"
        )
        rewritten = aot._replace_symbols(text, mapping, pattern)
        self.assertEqual(
            rewritten,
            "rac1_w158_sub_00100000 rac1_w158_sub_00100000_extra "
            "rac1_w158_FUN_00100020 prefix_sub_00100000 sub_00100000_suffix",
        )

    def test_expand_dense_table_keeps_existing_indices_and_reaches_level0_end(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "register_functions.cpp"
            path.write_text(
                "extern const uint32_t g_ps2RecompiledFunctionTableBase = 0x112380u;\n"
                "extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0x23d344u;\n"
                "extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = 306161u;\n"
                "PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[306161u] = {};\n"
                "g_ps2RecompiledFunctionTable[123] = foo; // 0x11256c\n",
                encoding="utf-8",
            )
            result = aot.expand_boot_function_table(path, 0x2F0CD0)
            text = path.read_text(encoding="utf-8")
            self.assertEqual(result["new_end"], 0x2F0CD0)
            self.assertEqual(result["slot_count"], (0x2F0CD0 - 0x112380) // 4)
            self.assertIn("g_ps2RecompiledFunctionTable[123] = foo", text)
            self.assertIn("g_ps2RecompiledFunctionTableEnd = 0x2f0cd0u", text)


    def test_generated_output_manifest_detects_missing_or_tampered_files(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "nested").mkdir()
            (root / "a.cpp").write_text("alpha\n", encoding="utf-8")
            (root / "nested" / "b.h").write_text("beta\n", encoding="utf-8")
            (root / "openratchet_overlay_build.json").write_text("{}\n", encoding="utf-8")
            expected = aot._directory_hashes(
                root, exclude_names=frozenset({"openratchet_overlay_build.json"})
            )
            self.assertEqual(set(expected), {"a.cpp", "nested/b.h"})
            self.assertTrue(aot._generated_outputs_match(root, expected))

            (root / "nested" / "b.h").write_text("tampered\n", encoding="utf-8")
            self.assertFalse(aot._generated_outputs_match(root, expected))
            (root / "nested" / "b.h").write_text("beta\n", encoding="utf-8")
            (root / "a.cpp").unlink()
            self.assertFalse(aot._generated_outputs_match(root, expected))

    def test_build_local_dense_table_never_shrinks_on_smaller_generation_rebuild(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / "generated" / "register_functions.cpp"
            output = root / "build" / "generated" / "register_functions.cpp"
            source.parent.mkdir(parents=True)
            source.write_text(
                "extern const uint32_t g_ps2RecompiledFunctionTableBase = 0x112380u;\n"
                "extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0x23d344u;\n"
                "extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = 306161u;\n"
                "PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[306161u] = {};\n",
                encoding="utf-8",
            )
            first = aot.write_expanded_boot_function_table(source, output, 0x2F0CD0)
            self.assertEqual(first["new_end"], 0x2F0CD0)
            second = aot.write_expanded_boot_function_table(source, output, 0x23CD18)
            self.assertEqual(second["new_end"], 0x2F0CD0)
            self.assertIn(
                "g_ps2RecompiledFunctionTableEnd = 0x2f0cd0u",
                output.read_text(encoding="utf-8"),
            )

    def test_build_local_dense_table_does_not_modify_boot_generated_source(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / "generated" / "register_functions.cpp"
            output = root / "build" / "generated" / "register_functions.cpp"
            source.parent.mkdir(parents=True)
            original = (
                "extern const uint32_t g_ps2RecompiledFunctionTableBase = 0x112380u;\n"
                "extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0x23d344u;\n"
                "extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = 306161u;\n"
                "PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[306161u] = {};\n"
                "g_ps2RecompiledFunctionTable[123] = foo; // 0x11256c\n"
            )
            source.write_text(original, encoding="utf-8")
            result = aot.write_expanded_boot_function_table(source, output, 0x2F0CD0)
            self.assertEqual(source.read_text(encoding="utf-8"), original)
            self.assertEqual(result["new_end"], 0x2F0CD0)
            built = output.read_text(encoding="utf-8")
            self.assertIn("g_ps2RecompiledFunctionTableEnd = 0x2f0cd0u", built)
            self.assertIn("g_ps2RecompiledFunctionTable[123] = foo", built)



if __name__ == "__main__":
    unittest.main()
