"""Conservative linked-ELF code/literal closure check for the radio ISR.

Public API: analyze_elf(elf_bytes, objdump_disassembly, entry=ENTRY_SYMBOL).
CLI: --elf firmware.elf --objdump xtensa-lx106-elf-objdump --report report.json.
Runtime stack/global object contents cannot be proven by this static check;
unresolved indirect control transfers are always rejected.
"""

import argparse
from collections import deque
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from typing import Any


ENTRY_SYMBOL = "IrqManager::processIRQ()"
IRAM = (0x40100000, 0x40110000)
DRAM = (0x3FFE8000, 0x40000000)
MMIO = (0x60000000, 0x60010000)
FLASH_DATA = (0x40200000, 0x60000000)
DELAY_ROM = 0x40002ECC
MAX_REFINEMENTS = 8
LIMITS = [
    "Checks linked code and l32r literals reachable from the selected ISR entry, not every firmware function.",
    "Runtime global/object pointers must reference DRAM objects; ordinary loads through these pointers and stack contents are not statically proven.",
    "Vtable-derived or other unresolved indirect calls/jumps are rejected; only literal/move-resolved control targets are accepted.",
    "Assumes the supplied objdump listing corresponds to this ELF and ordinary callees obey the Xtensa call0 ABI.",
    "Does not prove radio timing, interrupt latency, hardware behavior or protocol correctness.",
    "High integer masks outside mapped flash/MMIO may be scalar literals, but known invalid addresses cannot be dereferenced or used as control targets.",
]
_ADDRESS = re.compile(r"(?:0x)?([0-9a-fA-F]+)(?:\s|$)")
_REGISTER = re.compile(r"a(?:[0-9]|1[0-5])\Z")
_LABEL = re.compile(r"^\s*([0-9a-fA-F]+)\s+<(.+)>:\s*$")
_LINE = re.compile(r"^\s*([0-9a-fA-F]+):\s+(.+)$")
_HEX = re.compile(r"[0-9a-fA-F]+\Z")


class _ElfError(ValueError):
    pass


def _inside(address: int, bounds: tuple[int, int]) -> bool:
    return bounds[0] <= address < bounds[1]


def _cstring(data: bytes, offset: int) -> str:
    if not 0 <= offset < len(data):
        raise _ElfError("String table reference is outside its section")
    end = data.find(b"\0", offset)
    if end < 0:
        raise _ElfError("String table reference is not terminated")
    return data[offset:end].decode("utf-8", errors="replace")


class _Elf:
    def __init__(self, contents: bytes):
        self.contents = contents
        if len(contents) < 52:
            raise _ElfError("ELF header is truncated")
        fields = struct.unpack_from("<16sHHIIIIIHHHHHH", contents)
        ident = fields[0]
        if ident[:7] != b"\x7fELF\x01\x01\x01" or fields[2] != 94:
            raise _ElfError("Expected an ELF32 little-endian Xtensa image")
        offset, stride, count, names_index = fields[6], fields[11], fields[12], fields[13]
        if stride < 40 or not count or names_index >= count:
            raise _ElfError("ELF section table is missing or unsupported")
        if offset + stride * count > len(contents):
            raise _ElfError("ELF section table is truncated")
        self.sections = []
        for index in range(count):
            row = struct.unpack_from("<IIIIIIIIII", contents, offset + index * stride)
            section = dict(zip(
                ("name_offset", "type", "flags", "address", "offset", "size", "link", "info", "alignment", "entry_size"), row
            ))
            if section["type"] != 8 and section["offset"] + section["size"] > len(contents):
                raise _ElfError("ELF section contents are truncated")
            self.sections.append(section)
        names = self.section_bytes(names_index)
        for section in self.sections:
            section["name"] = _cstring(names, section["name_offset"])
        self.symbols = []
        for section in self.sections:
            if section["type"] != 2:
                continue
            stride = section["entry_size"]
            if stride < 16 or section["size"] % stride or section["link"] >= len(self.sections):
                raise _ElfError("ELF symbol table is malformed")
            strings = self.section_bytes(section["link"])
            for entry_offset in range(0, section["size"], stride):
                name, value, size, info, other, index = struct.unpack_from(
                    "<IIIBBH", contents, section["offset"] + entry_offset
                )
                self.symbols.append({
                    "name": _cstring(strings, name), "address": value, "size": size,
                    "type": info & 15, "section_index": index,
                })
        if not self.symbols:
            raise _ElfError("An unstripped ELF .symtab is required")
        self.functions = [symbol for symbol in self.symbols if symbol["type"] == 2]

    def section_bytes(self, index: int) -> bytes:
        section = self.sections[index]
        if section["type"] == 8:
            raise _ElfError("NOBITS section has no stored literal bytes")
        return self.contents[section["offset"]:section["offset"] + section["size"]]

    def literal(self, address: int) -> int:
        if address % 4:
            raise _ElfError("l32r literal storage is not word aligned")
        for index, section in enumerate(self.sections):
            if (
                section["flags"] & 2 and section["type"] != 8
                and section["address"] <= address
                and address + 4 <= section["address"] + section["size"]
            ):
                return struct.unpack_from("<I", self.section_bytes(index), address - section["address"])[0]
        raise _ElfError("Literal address has no file-backed allocated ELF section")

    def function(self, address: int) -> dict[str, Any] | None:
        # Zero-size aliases may share the body/range of a sized symbol.
        matches = [symbol for symbol in self.functions if symbol["address"] == address and symbol["size"]]
        if not matches:
            return None
        symbol = max(matches, key=lambda item: item["size"])
        index = symbol["section_index"]
        if index >= len(self.sections):
            return None
        section = self.sections[index]
        if not section["flags"] & 4:
            return None
        if not (section["address"] <= address and address + symbol["size"] <= section["address"] + section["size"]):
            return None
        return symbol

    def verified_delay(self) -> bool:
        return any(symbol["name"] == "ets_delay_us" and symbol["address"] == DELAY_ROM for symbol in self.symbols)


def _disassembly(text: str) -> tuple[dict[int, dict[str, Any]], dict[int, list[str]]]:
    instructions: dict[int, dict[str, Any]] = {}
    labels: dict[int, list[str]] = {}
    for line in text.splitlines():
        label = _LABEL.match(line)
        if label:
            labels.setdefault(int(label.group(1), 16), []).append(label.group(2))
            continue
        match = _LINE.match(line)
        if not match:
            continue
        tokens = match.group(2).split()
        encoding = []
        while tokens and _HEX.fullmatch(tokens[0]) and len(tokens[0]) % 2 == 0:
            encoding.append(tokens.pop(0))
        if not encoding or not tokens:
            continue
        address = int(match.group(1), 16)
        instruction = {
            "address": address, "size": sum(len(item) // 2 for item in encoding),
            "op": tokens[0].lower(), "args": " ".join(tokens[1:]),
        }
        if address in instructions and instructions[address] != instruction:
            raise ValueError("Conflicting disassembly at one instruction address")
        instructions[address] = instruction
    return instructions, labels


def _target(text: str) -> int | None:
    match = _ADDRESS.match(text.strip())
    return int(match.group(1), 16) if match else None


def _operands(text: str) -> list[str]:
    return [part.strip() for part in text.split("<", 1)[0].split(",")]


def _reg(text: str) -> str | None:
    value = text.strip().lower()
    if value == "sp":
        return "a1"
    return value if _REGISTER.fullmatch(value) else None


def _immediate(text: str) -> int | None:
    try:
        return int(text.strip(), 0)
    except ValueError:
        return None


class _Analyzer:
    def __init__(self, elf: _Elf, instructions: dict[int, dict[str, Any]], labels: dict[int, list[str]], entry: str):
        self.elf = elf
        self.instructions = instructions
        self.labels = labels
        self.entry = entry
        self.errors: list[dict[str, Any]] = []
        self.error_keys = set()
        self.pending: deque[int] = deque()
        self.queued = set()
        self.reachable: list[dict[str, Any]] = []

    def error(self, code: str, address: int | None, detail: str, **context: Any) -> None:
        key = (code, address, detail)
        if key not in self.error_keys:
            self.error_keys.add(key)
            error = {"code": code, "detail": detail}
            if address is not None:
                error["address"] = "0x%08x" % address
            error.update(context)
            self.errors.append(error)

    def name(self, address: int, fallback: str) -> str:
        names = self.labels.get(address, [])
        return next((name for name in names if "+0x" not in name), fallback)

    def code_target(self, target: int, origin: int) -> bool:
        if target >= 0x40200000:
            self.error("flash-code", origin, "Control target 0x%08x is outside IRAM in flash/other high memory" % target)
            return False
        if 0x40000000 <= target < 0x40100000:
            if target == DELAY_ROM and self.elf.verified_delay():
                return True
            self.error("rom-call", origin, "ROM control target 0x%08x is not the verified ets_delay_us" % target)
            return False
        if not _inside(target, IRAM):
            self.error("invalid-code", origin, "Control target 0x%08x is not allowed executable memory" % target)
            return False
        symbol = self.elf.function(target)
        if symbol is None:
            self.error("missing-symbol", origin, "IRAM target 0x%08x has no sized executable function symbol" % target)
            return False
        if target + symbol["size"] > IRAM[1]:
            self.error("invalid-code", origin, "Function range leaves the allowed IRAM region")
            return False
        if target not in self.queued:
            self.queued.add(target)
            self.pending.append(target)
        return True

    def literal(self, storage: int, origin: int) -> int | None:
        if not any(bounds[0] <= storage and storage + 4 <= bounds[1] for bounds in (IRAM, DRAM)):
            self.error("literal-storage", origin, "l32r storage 0x%08x is outside IRAM/DRAM" % storage)
            return None
        try:
            value = self.elf.literal(storage)
        except _ElfError as error:
            self.error("literal-storage", origin, str(error))
            return None
        if _inside(value, IRAM) or _inside(value, DRAM) or _inside(value, MMIO):
            return value
        if value == DELAY_ROM and self.elf.verified_delay():
            return value
        # Small unsigned/signed scalar constants cover register masks and flags.
        if value <= 0x00FFFFFF or value >= 0xFFFF0000:
            return value
        if _inside(value, FLASH_DATA):
            self.error("flash-literal", origin, "l32r loads forbidden mapped flash address 0x%08x" % value)
        elif 0x40000000 <= value < 0x40100000:
            self.error("rom-literal", origin, "l32r loads an unverified ROM address 0x%08x" % value)
        elif value >= MMIO[1]:
            # For example 0xfc0000ff masks SPI1U1 fields. It is not a mapped
            # flash pointer. Control and known-base memory uses are checked
            # separately, so accepting it as scalar cannot authorize a jump.
            return value
        else:
            self.error("unclassified-literal", origin, "l32r value 0x%08x is outside allowed data/code/scalar ranges" % value)
        return value

    def memory_access(self, instruction: dict[str, Any], state: dict[str, Any]) -> None:
        widths = {
            "l8ui": 1, "l16ui": 2, "l16si": 2, "l32i": 4, "l32i.n": 4,
            "l32e": 4, "s8i": 1, "s16i": 2, "s32i": 4, "s32i.n": 4,
            "s32e": 4, "s32c1i": 4,
        }
        width = widths.get(instruction["op"])
        if width is None:
            return
        args = _operands(instruction["args"])
        if len(args) < 3:
            self.error("invalid-data-address", instruction["address"], "Cannot parse ordinary memory operands")
            return
        base = state.get(_reg(args[1]))
        if not isinstance(base, int):
            # Stack/global object contents are explicit runtime assumptions;
            # they never establish a target for an indirect control transfer.
            return
        offset = _immediate(args[2])
        if offset is None:
            self.error("invalid-data-address", instruction["address"], "Cannot resolve offset from a known memory base")
            return
        address = (base + offset) & 0xFFFFFFFF
        if any(bounds[0] <= address and address + width <= bounds[1] for bounds in (IRAM, DRAM, MMIO)):
            return
        code = "flash-data" if _inside(address, FLASH_DATA) else "invalid-data-address"
        self.error(code, instruction["address"], "Known ordinary memory address 0x%08x is outside allowed IRAM/DRAM/MMIO" % address)

    def function(self, address: int) -> None:
        symbol = self.elf.function(address)
        if symbol is None:
            self.error("missing-symbol", address, "Reachable function has no usable symbol range")
            return
        end = address + symbol["size"]
        name = self.name(address, symbol["name"])
        self.reachable.append({"name": name, "address": "0x%08x" % address, "size": symbol["size"]})
        # Only symbol-sized instruction ranges are examined. Between-function
        # literal pools can be misleadingly printed as instructions by objdump.
        code = {pc: instruction for pc, instruction in self.instructions.items() if address <= pc < end}
        if address not in code:
            self.error(
                "missing-disassembly", address, "Reachable function entry has no decoded instruction",
                target="0x%08x" % address, function_end="0x%08x" % end,
            )
            return
        states: dict[int, dict[str, Any]] = {address: {"a1": "stack"}}
        work = deque([address])

        def propagate(destination: int, state: dict[str, Any], origin: int) -> None:
            if destination not in code:
                self.error(
                    "missing-instruction", origin, "In-function control target 0x%08x has no instruction" % destination,
                    target="0x%08x" % destination, function_end="0x%08x" % end,
                )
                return
            if destination not in states:
                states[destination] = state.copy()
                work.append(destination)
                return
            merged = {key: value for key, value in states[destination].items() if key in state and state[key] == value}
            if merged != states[destination]:
                states[destination] = merged
                work.append(destination)

        while work:
            pc = work.popleft()
            instruction = code[pc]
            size, op = instruction["size"], instruction["op"]
            if not size or pc + size > end:
                self.error("instruction-range", pc, "Instruction crosses its function symbol boundary")
                continue
            args = _operands(instruction["args"])
            state = states[pc].copy()
            self.memory_access(instruction, state)
            successors = []
            fallthrough = True

            def branch(target: int | None) -> None:
                if target is None:
                    self.error("unresolved-direct", pc, "Cannot parse a direct control target")
                elif address <= target < end:
                    successors.append(target)
                else:
                    self.code_target(target, pc)

            if op in {"ret", "ret.n"}:
                fallthrough = False
            elif op == "call0":
                target = _target(instruction["args"])
                if target is None:
                    self.error("unresolved-direct", pc, "Cannot parse direct call0 target")
                else:
                    self.code_target(target, pc)
                # call0 ABI: a12-a15 and SP are callee-saved. Other return/
                # argument/temporary registers cannot retain constant targets.
                state = {key: value for key, value in state.items() if key in {"a1", "a12", "a13", "a14", "a15"}}
            elif op in {"callx0", "jx"}:
                register = _reg(args[0]) if args else None
                target = state.get(register)
                if not isinstance(target, int):
                    self.error("unresolved-indirect", pc, op + " target is not a proven literal/move constant")
                elif op == "jx":
                    branch(target)
                else:
                    self.code_target(target, pc)
                if op == "jx":
                    fallthrough = False
                else:
                    state = {key: value for key, value in state.items() if key in {"a1", "a12", "a13", "a14", "a15"}}
            elif op == "j":
                branch(_target(instruction["args"]))
                fallthrough = False
            elif op.startswith("call") or op.startswith("ret") or op.startswith("loop"):
                self.error("unsupported-control", pc, "Unsupported control instruction " + op)
                fallthrough = False
            elif op.startswith("b") and not op.startswith("break"):
                branch(_target(args[-1]) if args else None)
            elif op == "l32r":
                destination = _reg(args[0]) if args else None
                storage = _target(args[1]) if len(args) > 1 else None
                if destination is None or storage is None:
                    self.error("literal-storage", pc, "Cannot parse l32r operands")
                else:
                    value = self.literal(storage, pc)
                    state.pop(destination, None)
                    if value is not None:
                        state[destination] = value
            elif op in {"mov", "mov.n"}:
                destination = _reg(args[0]) if args else None
                source = _reg(args[1]) if len(args) > 1 else None
                value = state.get(source)
                if destination:
                    state.pop(destination, None)
                    if value is not None:
                        state[destination] = value
            elif op in {"movi", "movi.n"}:
                destination = _reg(args[0]) if args else None
                value = _immediate(args[1]) if len(args) > 1 else None
                if destination:
                    state.pop(destination, None)
                    if value is not None:
                        state[destination] = value & 0xFFFFFFFF
            elif op in {"addi", "addi.n", "addmi"}:
                destination = _reg(args[0]) if args else None
                source = _reg(args[1]) if len(args) > 1 else None
                value = state.get(source)
                amount = _immediate(args[2]) if len(args) > 2 else None
                if destination:
                    state.pop(destination, None)
                    if value == "stack" and amount is not None:
                        state[destination] = "stack"
                    elif isinstance(value, int) and amount is not None:
                        state[destination] = (value + amount) & 0xFFFFFFFF
            elif op.startswith(".") or op in {"ill", "ill.n"}:
                self.error("undecoded-instruction", pc, "Reachable bytes are not a supported instruction")
                fallthrough = False
            elif not (
                op.startswith(("s8i", "s16i", "s32i", "wsr"))
                or op in {"memw", "isync", "rsync", "esync", "dsync", "nop", "nop.n", "ssl", "ssr"}
            ):
                # Ordinary loads (including vtable loads) destroy knowledge;
                # their runtime contents are never guessed from ELF initializers.
                destination = _reg(args[0]) if args else None
                if destination:
                    state.pop(destination, None)
            if fallthrough:
                if pc + size < end:
                    successors.append(pc + size)
                else:
                    self.error("unterminated-function", pc, "Function falls through beyond its symbol range")
            for successor in successors:
                propagate(successor, state, pc)

    def run(self) -> dict[str, Any]:
        entries = [address for address, names in self.labels.items() if self.entry in names]
        if len(entries) != 1:
            self.error("missing-entry", None, "Expected exactly one demangled entry label")
        elif self.code_target(entries[0], entries[0]):
            while self.pending:
                self.function(self.pending.popleft())
        return {
            "schema_version": 1, "entry": self.entry, "passed": not self.errors,
            "errors": self.errors,
            "reachable": sorted(self.reachable, key=lambda item: item["address"]),
            "limits": list(LIMITS),
        }


def analyze_elf(
    elf_bytes: bytes, disassembly: str, entry: str = ENTRY_SYMBOL
) -> dict[str, Any]:
    """Return a JSON-safe report with passed/errors/reachable/limits fields."""
    try:
        elf = _Elf(elf_bytes)
        instructions, labels = _disassembly(disassembly)
        report = _Analyzer(elf, instructions, labels, entry).run()
    except (ValueError, struct.error, IndexError, OverflowError) as error:
        report = {
            "schema_version": 1, "entry": entry, "passed": False,
            "errors": [{"code": "malformed-elf", "detail": str(error)}],
            "reachable": [], "limits": list(LIMITS),
        }
    report["elf_sha256"] = hashlib.sha256(elf_bytes).hexdigest()
    return report


def _objdump(objdump: str, elf: Path, start: int | None = None, stop: int | None = None) -> str:
    command = [objdump, "-d", "-C"]
    if start is not None and stop is not None:
        command.extend(["--start-address=0x%08x" % start, "--stop-address=0x%08x" % stop])
    command.append(str(elf))
    return subprocess.run(
        command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=60,
    ).stdout


def _refine_analysis(contents: bytes, listing: str, objdump: str, elf: Path, entry: str) -> dict[str, Any]:
    """Recover exact branch-entry decoding without trusting padding alignment.

    The pure analyze_elf API never fills missing bytes. Only the CLI can ask the
    same objdump to decode at a proven target, bounded by its real symbol end.
    Repeated/failed requests remain errors and the number of requests is capped.
    """
    report = analyze_elf(contents, listing, entry)
    attempted: set[tuple[int, int]] = set()
    ranges = []
    while len(attempted) < MAX_REFINEMENTS:
        request = None
        for error in report["errors"]:
            if error["code"] not in {"missing-instruction", "missing-disassembly"}:
                continue
            try:
                start = int(error["target"], 16)
                stop = int(error["function_end"], 16)
            except (KeyError, TypeError, ValueError):
                continue
            if IRAM[0] <= start < stop <= IRAM[1] and (start, stop) not in attempted:
                request = (start, stop)
                break
        if request is None:
            break
        attempted.add(request)
        start, stop = request
        ranges.append({"start": "0x%08x" % start, "stop": "0x%08x" % stop})
        try:
            refined = _objdump(objdump, elf, start, stop)
        except (OSError, subprocess.SubprocessError) as error:
            report["passed"] = False
            report["errors"].append({
                "code": "refinement-tool-error",
                "detail": type(error).__name__ + ": could not decode the missing target range",
            })
            break
        listing += "\n" + refined
        report = analyze_elf(contents, listing, entry)
    report["refinements"] = {
        "attempts": len(attempted), "maximum": MAX_REFINEMENTS, "ranges": ranges,
    }
    return report


def main(argv: list[str] | None = None) -> int:
    """Run objdump without a shell, analyze its linked ELF, and write a report."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--entry", default=ENTRY_SYMBOL)
    arguments = parser.parse_args(argv)
    try:
        contents = arguments.elf.read_bytes()
        report = _refine_analysis(
            contents, _objdump(arguments.objdump, arguments.elf),
            arguments.objdump, arguments.elf, arguments.entry,
        )
    except (OSError, subprocess.SubprocessError) as error:
        report = {
            "schema_version": 1, "entry": arguments.entry, "passed": False,
            "errors": [{"code": "tool-error", "detail": type(error).__name__ + ": could not read ELF or run objdump"}],
            "reachable": [], "limits": list(LIMITS),
        }
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("IRQ IRAM closure: " + ("PASS" if report["passed"] else "FAIL"))
    for error in report["errors"]:
        print(error["code"] + " " + error.get("address", "") + ": " + error["detail"])
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
