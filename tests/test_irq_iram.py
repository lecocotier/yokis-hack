"""Synthetic ELF32 fixtures exercise the actual linked IRQ closure analyzer."""

import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools/check_irq_iram.py"
SPEC = importlib.util.spec_from_file_location("check_irq_iram", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)

ROOT = 0x40100040
HANDLER = 0x40100100
LITERAL = 0x40100020
ENTRY = checker.ENTRY_SYMBOL


def elf_fixture(symbols, literals=None):
    """Build an ELF with genuine section and symbol tables, not parser mocks."""
    text = bytearray(0x1000)
    data = bytearray(0x1000)
    flash = bytearray(0x1000)
    contents = [(0x40100000, text), (0x3FFE8000, data), (0x40200000, flash)]
    for address, value in (literals or {}).items():
        for base, section in contents:
            if base <= address <= base + len(section) - 4:
                struct.pack_into("<I", section, address - base, value)
                break
        else:
            raise AssertionError("Fixture literal has no section")
    strings = bytearray(b"\0")
    symtab = bytearray(b"\0" * 16)
    for name, address, size in symbols:
        offset = len(strings)
        strings.extend(name.encode("utf-8") + b"\0")
        section_index = 0xFFF1
        for index, (base, section) in enumerate(contents, 1):
            if base <= address < base + len(section):
                section_index = index
                break
        symtab.extend(struct.pack("<IIIBBH", offset, address, size, 0x12, 0, section_index))
    section_names = b"\0.text1\0.data\0.irom0.text\0.symtab\0.strtab\0.shstrtab\0"
    sections = [
        (".text1", 1, 6, 0x40100000, bytes(text), 0, 0, 4, 0),
        (".data", 1, 3, 0x3FFE8000, bytes(data), 0, 0, 4, 0),
        (".irom0.text", 1, 6, 0x40200000, bytes(flash), 0, 0, 4, 0),
        (".symtab", 2, 0, 0, bytes(symtab), 5, 1, 4, 16),
        (".strtab", 3, 0, 0, bytes(strings), 0, 0, 1, 0),
        (".shstrtab", 3, 0, 0, section_names, 0, 0, 1, 0),
    ]
    result = bytearray(b"\0" * 52)
    headers = [b"\0" * 40]
    for name, kind, flags, address, payload, link, info, alignment, entsize in sections:
        while len(result) % alignment:
            result.append(0)
        offset = len(result)
        result.extend(payload)
        name_offset = section_names.index(name.encode() + b"\0")
        headers.append(struct.pack("<IIIIIIIIII", name_offset, kind, flags, address, offset, len(payload), link, info, alignment, entsize))
    while len(result) % 4:
        result.append(0)
    section_offset = len(result)
    result.extend(b"".join(headers))
    ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    result[:52] = struct.pack("<16sHHIIIIIHHHHHH", ident, 2, 94, 1, ROOT, 0, section_offset, 0, 52, 0, 0, 40, len(headers), 6)
    return bytes(result)


def listing(body, extra=""):
    return f"\n{ROOT:08x} <{ENTRY}>:\n" + body + extra


class IrqIramTests(unittest.TestCase):
    def analyze(self, body, symbols=None, literals=None, extra="", root_size=5):
        elf = elf_fixture([(ENTRY, ROOT, root_size)] + list(symbols or []), literals)
        return checker.analyze_elf(elf, listing(body, extra))

    def assert_error(self, report, code):
        self.assertFalse(report["passed"])
        self.assertIn(code, [error["code"] for error in report["errors"]])

    def test_flash_direct_call_is_rejected(self):
        report = self.analyze(
            "40100040: 000005 call0 40200100 <flashFn()>\n40100043: f00d ret.n\n",
            symbols=[("flashFn()", 0x40200100, 2)],
        )
        self.assert_error(report, "flash-code")

    def test_literal_storage_in_flash_is_rejected_even_for_dram_value(self):
        report = self.analyze(
            "40100040: fff721 l32r a2, 40200020 <pool>\n40100043: f00d ret.n\n",
            literals={0x40200020: 0x3FFE9000},
        )
        self.assert_error(report, "literal-storage")

    def test_flash_address_loaded_from_iram_literal_is_rejected(self):
        report = self.analyze(
            "40100040: fff721 l32r a2, 40100020 <pool>\n40100043: f00d ret.n\n",
            literals={LITERAL: 0x40200100},
        )
        self.assert_error(report, "flash-literal")

    def test_unknown_indirect_call_and_jump_are_rejected(self):
        for instruction in ("callx0 a9", "jx a9"):
            with self.subTest(instruction=instruction):
                report = self.analyze("40100040: 0009a0 " + instruction + "\n40100043: f00d ret.n\n")
                self.assert_error(report, "unresolved-indirect")

    def test_iram_tail_target_resolves_through_literal_and_register_move(self):
        report = self.analyze(
            "40100040: fff721 l32r a2, 40100020 <pool>\n"
            "40100043: 029d mov.n a9, a2\n"
            "40100045: 0009a0 jx a9\n",
            symbols=[("irqHandler()", HANDLER, 2)],
            literals={LITERAL: HANDLER}, root_size=8,
            extra="40100100 <irqHandler()>:\n40100100: f00d ret.n\n",
        )
        self.assertTrue(report["passed"], report["errors"])
        self.assertIn("irqHandler()", [item["name"] for item in report["reachable"]])

    def test_only_verified_delay_rom_address_and_symbol_are_allowed(self):
        report = self.analyze(
            "40100040: 000005 call0 40002ecc <ets_delay_us>\n40100043: f00d ret.n\n",
            symbols=[("ets_delay_us", 0x40002ECC, 0)],
        )
        self.assertTrue(report["passed"], report["errors"])
        wrong_name = self.analyze(
            "40100040: 000005 call0 40002ecc <unverified_rom>\n40100043: f00d ret.n\n",
            symbols=[("unverified_rom", 0x40002ECC, 0)],
        )
        self.assert_error(wrong_name, "rom-call")

    def test_other_rom_callee_is_rejected(self):
        report = self.analyze(
            "40100040: 000005 call0 40001000 <memcpy>\n40100043: f00d ret.n\n",
            symbols=[("memcpy", 0x40001000, 0)],
        )
        self.assert_error(report, "rom-call")

    def test_malformed_elf_and_missing_entry_are_failures(self):
        self.assert_error(checker.analyze_elf(b"not ELF", ""), "malformed-elf")
        self.assert_error(checker.analyze_elf(elf_fixture([]), ""), "missing-entry")

    def test_branch_inside_own_symbol_is_not_a_recursive_graph_edge(self):
        report = self.analyze("40100040: ffff06 j 40100040 <IrqManager::processIRQ()>\n", root_size=3)
        self.assertTrue(report["passed"], report["errors"])
        self.assertEqual(len(report["reachable"]), 1)

    def test_literal_pool_between_functions_is_not_disassembled_as_function_code(self):
        report = self.analyze(
            "40100040: f00d ret.n\n"
            "40100042: 000005 call0 40200100 <pool_looks_like_call>\n",
            root_size=2,
        )
        self.assertTrue(report["passed"], report["errors"])

    def test_small_constants_and_dram_global_loads_are_allowed_with_explicit_limits(self):
        report = self.analyze(
            "40100040: fff721 l32r a2, 40100020 <pool>\n"
            "40100043: 0228 l32i.n a2, a2, 0\n"
            "40100045: f00d ret.n\n",
            literals={LITERAL: 0x3FFE9000}, root_size=7,
        )
        self.assertTrue(report["passed"], report["errors"])
        self.assertIn("runtime", " ".join(report["limits"]).lower())
        small = self.analyze(
            "40100040: fff721 l32r a2, 40100020 <pool>\n40100043: f00d ret.n\n",
            literals={LITERAL: 0x70},
        )
        self.assertTrue(small["passed"], small["errors"])

    def test_flash_call_in_transitive_iram_callee_is_rejected(self):
        report = self.analyze(
            "40100040: 000005 call0 40100100 <irqHandler()>\n40100043: f00d ret.n\n",
            symbols=[("irqHandler()", HANDLER, 5), ("flashFn()", 0x40200100, 2)],
            extra="40100100 <irqHandler()>:\n40100100: 000005 call0 40200100 <flashFn()>\n40100103: f00d ret.n\n",
        )
        self.assert_error(report, "flash-code")
        self.assertEqual(len(report["reachable"]), 2)

    def test_zero_size_alias_uses_sized_symbol_at_same_address(self):
        report = self.analyze(
            "40100040: 000005 call0 40100100 <emptyAlias()>\n40100043: f00d ret.n\n",
            symbols=[("emptyAlias()", HANDLER, 0), ("actualBody()", HANDLER, 2)],
            extra="40100100 <emptyAlias()>:\n40100100: f00d ret.n\n",
        )
        self.assertTrue(report["passed"], report["errors"])

    def test_runtime_load_destroys_previous_literal_control_target(self):
        report = self.analyze(
            "40100040: fff791 l32r a9, 40100020 <pool>\n"
            "40100043: 0298 l32i.n a9, a2, 0\n"
            "40100045: 0009c0 callx0 a9\n"
            "40100048: f00d ret.n\n",
            symbols=[("irqHandler()", HANDLER, 2)], literals={LITERAL: HANDLER}, root_size=10,
        )
        self.assert_error(report, "unresolved-indirect")

    def test_branch_join_does_not_reuse_one_paths_literal_target(self):
        report = self.analyze(
            "40100040: 000216 beqz a2, 40100049 <other_path>\n"
            "40100043: fff791 l32r a9, 40100020 <pool>\n"
            "40100046: 000006 j 4010004b <joined>\n"
            "40100049: 0398 l32i.n a9, a3, 0\n"
            "4010004b: 0009c0 callx0 a9\n"
            "4010004e: f00d ret.n\n",
            symbols=[("irqHandler()", HANDLER, 2)], literals={LITERAL: HANDLER}, root_size=16,
            extra="40100100 <irqHandler()>:\n40100100: f00d ret.n\n",
        )
        self.assert_error(report, "unresolved-indirect")

    def test_verified_delay_can_be_a_literal_resolved_indirect_call(self):
        report = self.analyze(
            "40100040: fff791 l32r a9, 40100020 <pool>\n"
            "40100043: 0009c0 callx0 a9\n"
            "40100046: f00d ret.n\n",
            symbols=[("ets_delay_us", 0x40002ECC, 0)],
            literals={LITERAL: 0x40002ECC}, root_size=8,
        )
        self.assertTrue(report["passed"], report["errors"])

    def test_cli_runs_objdump_as_arguments_and_writes_report(self):
        elf = elf_fixture([(ENTRY, ROOT, 2)])
        output = listing("40100040: f00d ret.n\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            firmware = path / "firmware.elf"
            report_path = path / "report.json"
            firmware.write_bytes(elf)
            result = checker.subprocess.CompletedProcess([], 0, output, "")
            with mock.patch.object(checker.subprocess, "run", return_value=result) as run:
                with mock.patch("builtins.print"):
                    code = checker.main([
                        "--elf", str(firmware), "--objdump", "toolchain-objdump",
                        "--report", str(report_path),
                    ])
            self.assertEqual(code, 0)
            self.assertTrue(json.loads(report_path.read_text())["passed"])
            self.assertEqual(run.call_args.args[0], ["toolchain-objdump", "-d", "-C", str(firmware)])
            self.assertFalse(run.call_args.kwargs.get("shell", False))

    def test_high_spi_bitmask_literal_is_allowed_as_an_integer_operand(self):
        report = self.analyze(
            "40100040: fff731 l32r a3, 40100020 <mask>\n"
            "40100043: 103230 and a2, a2, a3\n"
            "40100046: f00d ret.n\n",
            literals={LITERAL: 0xFC0000FF}, root_size=8,
        )
        self.assertTrue(report["passed"], report["errors"])

    def test_high_integer_literal_cannot_be_dereferenced(self):
        report = self.analyze(
            "40100040: fff731 l32r a3, 40100020 <mask>\n"
            "40100043: 0328 l32i.n a2, a3, 0\n"
            "40100045: f00d ret.n\n",
            literals={LITERAL: 0xFC0000FF}, root_size=7,
        )
        self.assert_error(report, "invalid-data-address")

    def test_high_integer_literal_cannot_be_a_control_target(self):
        report = self.analyze(
            "40100040: fff791 l32r a9, 40100020 <mask>\n"
            "40100043: 0009a0 jx a9\n",
            literals={LITERAL: 0xFC0000FF}, root_size=6,
        )
        self.assert_error(report, "flash-code")

    def test_cli_refines_missing_branch_target_from_exact_address(self):
        elf = elf_fixture([(ENTRY, ROOT, 10)])
        initial = listing(
            "40100040: 000006 j 40100048 <real_target>\n"
            "40100043: 20c000 or a12, a0, a0\n"
            "40100046: 023800 andb b3, b8, b0\n"
        )
        self.assert_error(checker.analyze_elf(elf, initial), "missing-instruction")
        refined = "40100048 <IrqManager::processIRQ()+0x8>:\n40100048: f00d ret.n\n"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            firmware = path / "firmware.elf"
            report_path = path / "report.json"
            firmware.write_bytes(elf)
            results = [
                checker.subprocess.CompletedProcess([], 0, initial, ""),
                checker.subprocess.CompletedProcess([], 0, refined, ""),
            ]
            with mock.patch.object(checker.subprocess, "run", side_effect=results) as run:
                with mock.patch("builtins.print"):
                    code = checker.main([
                        "--elf", str(firmware), "--objdump", "toolchain-objdump",
                        "--report", str(report_path),
                    ])
            self.assertEqual(code, 0)
            report = json.loads(report_path.read_text())
            self.assertTrue(report["passed"], report["errors"])
            self.assertEqual(report["refinements"]["attempts"], 1)
            self.assertIn("--start-address=0x40100048", run.call_args_list[1].args[0])
            self.assertIn("--stop-address=0x4010004a", run.call_args_list[1].args[0])

    def test_cli_failed_refinement_remains_failed_and_is_not_retried_forever(self):
        elf = elf_fixture([(ENTRY, ROOT, 10)])
        initial = listing("40100040: 000006 j 40100048 <real_target>\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            firmware = path / "firmware.elf"
            report_path = path / "report.json"
            firmware.write_bytes(elf)
            results = [
                checker.subprocess.CompletedProcess([], 0, initial, ""),
                checker.subprocess.CompletedProcess([], 0, "", ""),
            ]
            with mock.patch.object(checker.subprocess, "run", side_effect=results) as run:
                with mock.patch("builtins.print"):
                    code = checker.main([
                        "--elf", str(firmware), "--objdump", "toolchain-objdump",
                        "--report", str(report_path),
                    ])
            self.assertEqual(code, 1)
            report = json.loads(report_path.read_text())
            self.assert_error(report, "missing-instruction")
            self.assertEqual(report["refinements"]["attempts"], 1)
            self.assertEqual(run.call_count, 2)


if __name__ == "__main__":
    unittest.main()
