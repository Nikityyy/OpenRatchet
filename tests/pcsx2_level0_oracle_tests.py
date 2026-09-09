from __future__ import annotations

import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pcsx2_level0_oracle as oracle


class OracleTests(unittest.TestCase):
    def test_delta_roundtrip_and_rejects_corruption(self) -> None:
        base = bytes((i * 7) & 0xFF for i in range(oracle.PAGE_BYTES * 3))
        current = bytearray(base)
        current[17] ^= 0xFF
        current[oracle.PAGE_BYTES + 5] ^= 0x44
        delta, summary = oracle.encode_page_delta(base, bytes(current))

        self.assertEqual(summary["changed_pages"], 2)
        self.assertEqual(oracle.decode_page_delta(base, delta), bytes(current))
        with self.assertRaisesRegex(oracle.OracleError, "Invalid ORDELTA1"):
            oracle.decode_page_delta(base, b"bad")

    def test_retail_ratchet_identity_from_proved_pool_layout(self) -> None:
        ram = bytearray(oracle.EE_RAM_BYTES)
        base = 0x00100000
        last = base + 0x3F00
        ram[oracle.MOBY_POOL_PTR:oracle.MOBY_POOL_PTR + 4] = base.to_bytes(4, "little")
        ram[oracle.MOBY_LAST_PTR:oracle.MOBY_LAST_PTR + 4] = last.to_bytes(4, "little")

        ratchet = base
        ram[ratchet + oracle.MOBY_TRAVERSAL_OFFSET] = 0
        ram[ratchet + oracle.MOBY_OCLASS_OFFSET:ratchet + oracle.MOBY_OCLASS_OFFSET + 2] = (0).to_bytes(2, "little")
        pvar = 0x00120000
        state = 0x00130000
        ram[ratchet + oracle.MOBY_PVAR_OFFSET:ratchet + oracle.MOBY_PVAR_OFFSET + 4] = pvar.to_bytes(4, "little")
        ram[pvar:pvar + 4] = state.to_bytes(4, "little")
        ram[state + oracle.RATCHET_STATE_RATCHET_OFFSET:state + oracle.RATCHET_STATE_RATCHET_OFFSET + 4] = ratchet.to_bytes(4, "little")
        # Slot 1 terminates the Retail traversal.
        ram[base + oracle.MOBY_RECORD_BYTES + oracle.MOBY_TRAVERSAL_OFFSET] = 0xFF

        pool_base, pool_last, found, found_pvar, found_state = oracle.find_ratchet_from_pool(bytes(ram))
        self.assertEqual((pool_base, pool_last), (base, last))
        self.assertEqual((found, found_pvar, found_state), (ratchet, pvar, state))

        semantic = oracle.semantic_from_ram(bytes(ram))
        self.assertEqual(semantic.ratchet_moby, ratchet)
        self.assertEqual(semantic.ratchet_pvar, pvar)
        self.assertEqual(semantic.ratchet_state, state)
        ranges = {item.name: (item.start, item.end) for item in oracle.build_watch_ranges(semantic)}
        self.assertEqual(ranges["ratchet-moby"], (ratchet, ratchet + 0x100))
        self.assertEqual(ranges["ratchet-pvar"], (pvar, pvar + 0x1000))
        self.assertEqual(ranges["ratchet-state"], (state, state + 0x400))


    def test_live_probe_requires_published_retail_pool(self) -> None:
        ram = bytearray(oracle.EE_RAM_BYTES)
        base = 0x00100000
        last = base + 0x3F00
        ram[oracle.MOBY_POOL_PTR:oracle.MOBY_POOL_PTR + 4] = base.to_bytes(4, "little")
        ram[oracle.MOBY_LAST_PTR:oracle.MOBY_LAST_PTR + 4] = last.to_bytes(4, "little")
        ram[base + oracle.MOBY_TRAVERSAL_OFFSET] = 0
        ram[base + oracle.MOBY_OCLASS_OFFSET:base + oracle.MOBY_OCLASS_OFFSET + 2] = (0).to_bytes(2, "little")
        ram[base + oracle.MOBY_RECORD_BYTES + oracle.MOBY_TRAVERSAL_OFFSET] = 0xFF

        class FakeClient:
            def read_memory(self, _cpu: str, address: int, length: int) -> str:
                return bytes(ram[address:address + length]).hex()

        ready, reason, details = oracle.probe_live_moby_pool(FakeClient())
        self.assertTrue(ready, reason)
        self.assertEqual(details["base"], base)
        self.assertEqual(details["ratchet"], base)

        ram[oracle.MOBY_POOL_PTR:oracle.MOBY_POOL_PTR + 4] = (0x535F5343).to_bytes(4, "little")
        ready, reason, _ = oracle.probe_live_moby_pool(FakeClient())
        self.assertFalse(ready)
        self.assertIn("not published", reason)

    def test_level0_generation_probe_accepts_overlay_and_distinguishes_boot(self) -> None:
        class FakeClient:
            def __init__(self, payload: bytes) -> None:
                self.payload = payload

            def read_memory(self, _cpu: str, address: int, length: int) -> str:
                self.assert_args = (address, length)
                return self.payload[:length].hex()

        overlay = FakeClient(oracle.LEVEL0_OVERLAY_PROBE_BYTES)
        self.assertEqual(oracle.level0_generation_probe(overlay)["generation"], "level0-overlay")
        self.assertEqual(oracle.require_level0_overlay_resident(overlay)["generation"], "level0-overlay")

        boot = FakeClient(oracle.BOOT_ELF_PROBE_BYTES)
        self.assertEqual(oracle.level0_generation_probe(boot)["generation"], "boot-elf")
        with self.assertRaisesRegex(oracle.OracleError, "still on the boot-ELF"):
            oracle.require_level0_overlay_resident(boot)

        unknown = FakeClient(bytes(len(oracle.LEVEL0_OVERLAY_PROBE_BYTES)))
        self.assertEqual(oracle.level0_generation_probe(unknown)["generation"], "unknown")
        with self.assertRaisesRegex(oracle.OracleError, "matches neither"):
            oracle.require_level0_overlay_resident(unknown)

    def test_changed_page_stats_and_writer_ranking(self) -> None:
        idle = bytes(oracle.PAGE_BYTES * 3)
        forward = bytearray(idle)
        backward = bytearray(idle)
        left = bytearray(idle)
        right = bytearray(idle)

        # Page 1 changes in every movement scenario; page 2 only in forward.
        for buf in (forward, backward, left, right):
            buf[oracle.PAGE_BYTES + 8] = 1
        forward[2 * oracle.PAGE_BYTES + 4] = 7

        stats = oracle.changed_page_stats(idle, bytes(forward))
        self.assertEqual([item["page_index"] for item in stats], [1, 2])
        ranked = oracle.rank_writer_pages(
            "move_forward",
            idle,
            {
                "move_forward": bytes(forward),
                "move_backward": bytes(backward),
                "move_left": bytes(left),
                "move_right": bytes(right),
            },
            ("move_forward", "move_backward", "move_left", "move_right"),
            limit=2,
        )
        self.assertEqual(ranked[0]["page_index"], 1)
        self.assertEqual(ranked[0]["family_frequency"], 4)

    def test_scenario_manifest_is_strict(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "scenarios.json"
            path.write_text(json.dumps({
                "schema": 1,
                "scenarios": [
                    {"name": "forward", "instruction": "Hold forward"},
                    {"name": "camera_right", "instruction": "Hold camera right"},
                ],
            }))
            self.assertEqual([x["name"] for x in oracle.scenario_defs(path)], ["forward", "camera_right"])

            path.write_text(json.dumps({
                "schema": 1,
                "scenarios": [
                    {"name": "same", "instruction": "A"},
                    {"name": "same", "instruction": "B"},
                ],
            }))
            with self.assertRaisesRegex(oracle.OracleError, "Duplicate scenario"):
                oracle.scenario_defs(path)

    def test_phase12_manifest_covers_seven_core_input_paths(self) -> None:
        path = REPO_ROOT / "tools" / "retail-oracle-level0-phase12.json"
        names = [item["name"] for item in oracle.scenario_defs(path)]
        self.assertEqual(
            names,
            [
                "idle",
                "move_forward",
                "move_left",
                "camera_right",
                "camera_up",
                "jump",
                "attack",
            ],
        )


    def test_pe_export_parser_finds_eemem(self) -> None:
        # Minimal synthetic PE32+ with one section and one exported data symbol.
        data = bytearray(0x1400)
        data[0:2] = b"MZ"
        pe = 0x80
        data[0x3C:0x40] = pe.to_bytes(4, "little")
        data[pe:pe + 4] = b"PE\0\0"
        data[pe + 4:pe + 6] = (0x8664).to_bytes(2, "little")
        data[pe + 6:pe + 8] = (1).to_bytes(2, "little")
        optional_size = 0xF0
        data[pe + 20:pe + 22] = optional_size.to_bytes(2, "little")
        optional = pe + 24
        data[optional:optional + 2] = (0x20B).to_bytes(2, "little")
        export_rva = 0x1000
        data[optional + 112:optional + 120] = export_rva.to_bytes(4, "little") + (0x100).to_bytes(4, "little")
        section = optional + optional_size
        data[section:section + 8] = b".rdata\0\0"
        data[section + 8:section + 12] = (0x1000).to_bytes(4, "little")
        data[section + 12:section + 16] = (0x1000).to_bytes(4, "little")
        data[section + 16:section + 20] = (0x1000).to_bytes(4, "little")
        data[section + 20:section + 24] = (0x200).to_bytes(4, "little")

        def off(rva: int) -> int:
            return 0x200 + (rva - 0x1000)

        functions_rva = 0x1050
        names_rva = 0x1060
        ordinals_rva = 0x1070
        name_rva = 0x1080
        fields = (0, 0, 0, 0, 0, 1, 1, 1, functions_rva, names_rva, ordinals_rva)
        import struct
        struct.pack_into("<IIHHIIIIIII", data, off(export_rva), *fields)
        struct.pack_into("<I", data, off(functions_rva), 0x2345)
        struct.pack_into("<I", data, off(names_rva), name_rva)
        struct.pack_into("<H", data, off(ordinals_rva), 0)
        data[off(name_rva):off(name_rva) + 6] = b"EEmem\0"

        with tempfile.TemporaryDirectory() as td:
            image = Path(td) / "pcsx2-qt.exe"
            image.write_bytes(data)
            self.assertEqual(oracle.pe_export_rva(image, "EEmem"), 0x2345)

    def test_memcheck_hit_counter_is_authoritative_not_stop_pc(self) -> None:
        self.assertEqual(oracle.memcheck_hit_count({"hits": 7, "last_pc": "0x1234"}), 7)
        self.assertEqual(oracle.memcheck_hit_count({}), 0)

    def test_inspect_archive_reconstructs_sparse_snapshot(self) -> None:
        base = bytes([0]) * (oracle.PAGE_BYTES * 2)
        current = bytearray(base)
        current[oracle.PAGE_BYTES + 10] = 0x77
        delta, summary = oracle.encode_page_delta(base, bytes(current))
        summary["post_ram_sha256"] = oracle.sha256_bytes(bytes(current))
        summary["writer_events"] = 3

        with tempfile.TemporaryDirectory() as td:
            archive_path = Path(td) / "oracle.zip"
            manifest = {
                "kind": "RAC1_RETAIL_ORACLE_LEVEL0",
                "game": {"title": "test", "id": "TEST"},
                "baseline": {"ee_ram_sha256": oracle.sha256_bytes(base)},
                "scenarios": [{"name": "forward", "delta": summary}],
            }
            with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("manifest.json", json.dumps(manifest))
                archive.writestr("baseline/ee_ram.bin", base)
                archive.writestr("scenarios/forward/ee_ram.delta", delta)
            self.assertEqual(oracle.inspect_archive(archive_path), 0)


if __name__ == "__main__":
    unittest.main()
