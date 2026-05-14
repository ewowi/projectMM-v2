#!/usr/bin/env python3
"""Estimate and report class sizes for all MoonModule subclasses in src/.

Parses .h files with regex to find classes that inherit from MoonModule,
extracts their non-static member fields, and computes an estimated size:
  estimated = MOONMODULE_BASE + sum(own fields, with alignment padding)

MoonModule base size is computed from MoonModule.h's known field layout.
For complex types (std::unique_ptr, std::string, FrameRing, etc.) a lookup
table provides known sizes on a 64-bit host (the PC target). Unknown types
are flagged so you can add them to the table.

Exit 0 — all classes resolved cleanly.
Exit 1 — any unknown types found (add them to TYPE_SIZES below).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------------------
# Known type sizes on a 64-bit host (the PC build target).
# Add entries here whenever the checker reports an unknown type.
# ---------------------------------------------------------------------------
TYPE_SIZES: dict[str, int] = {
    # primitives
    "bool":     1,
    "int8_t":   1, "uint8_t":  1,
    "int16_t":  2, "uint16_t": 2,
    "int32_t":  4, "uint32_t": 4,
    "int64_t":  8, "uint64_t": 8,
    "float":    4,
    "double":   8,
    "int":      4,
    "unsigned": 4,
    "size_t":   8,
    "uintptr_t": 8,
    # stdlib
    "std::string":  24,   # SSO string: 24B on 64-bit libstdc++/libc++
    "std::unique_ptr": 8, # one pointer
    # stdlib containers (64-bit)
    "std::vector":  24,   # pointer + size + capacity
    "std::map":     48,   # red-black tree header (libstdc++)
    # project types
    "RGB":          3,
    "FrameRing":   32,    # 2× pointer + 2× uint32 + mutex-like state; see FrameRing.h
    "JsonDocument": 128,  # ArduinoJson 7 minimum slab
    # pal types
    "Udp":          4,    # PC: one int fd_; ESP32: WiFiUDP (~52B) — using PC value
}

# MoonModule base fields (from MoonModule.h protected + public sections).
# Manually maintained — update when MoonModule.h fields change.
MOONMODULE_BASE_FIELDS: list[tuple[str, int]] = [
    # Fields ordered 8B → 4B → 2B → 1B to eliminate padding (96 B total).
    ("id_",               24),   # std::string
    ("type_",              8),   # const char*
    ("manager_",           8),   # ModuleManager*
    ("controls_",          8),   # ControlDescriptor*
    ("children_",          8),   # MoonModule**
    ("pendingProps_",      8),   # JsonDocument*
    ("tickCount_",         4),   # uint32_t
    ("timingPrevTicks_",   4),   # uint32_t
    ("timingPrevMs_",      4),   # uint32_t
    ("moduleAllocBytes_",  4),   # uint32_t (was size_t)
    ("classSize_",         2),   # uint16_t (was size_t; max ~600B fits)
    ("usPerTick_",         2),   # uint16_t
    ("enabled_",           1),   # bool
    ("schemaDirty_",       1),   # bool
    ("core_",              1),   # uint8_t
    ("controlCount_",      1),   # uint8_t
    ("controlCapacity_",   1),   # uint8_t
    ("childCount_",        1),   # uint8_t
    ("childCapacity_",     1),   # uint8_t
]


def _aligned(offset: int, align: int) -> int:
    """Round offset up to next multiple of align."""
    return (offset + align - 1) & ~(align - 1)


def _layout_size(fields: list[tuple[str, int]]) -> int:
    """Compute struct size respecting natural alignment, ending with struct-alignment pad."""
    if not fields:
        return 0
    offset = 0
    max_align = 1
    for _name, size in fields:
        align = min(size, 8)  # max natural alignment is 8B on 64-bit
        max_align = max(max_align, align)
        offset = _aligned(offset, align)
        offset += size
    # Final struct padding
    offset = _aligned(offset, max_align)
    return offset


MOONMODULE_BASE = _layout_size(MOONMODULE_BASE_FIELDS)


def _parse_type(decl: str) -> tuple[str | None, int]:
    """Given a field declaration string, return (canonical_type, byte_size) or (None, 0).

    Handles:
      uint8_t foo_ = 0;
      std::unique_ptr<pal::WsServer> ws_;
      RGB* pixels_ = nullptr;          → pointer (8B)
      RGB** children_ = nullptr;       → pointer (8B)
      uint8_t width_ = 16;
      FrameRing ring_;
      char ssid_[33] = {};             → char[N], N bytes
    """
    decl = decl.strip().rstrip(";")
    # Strip initialiser (including brace-init and string literals)
    decl = re.sub(r"\s*=\s*.*$", "", decl).strip()

    # char name_[N]  →  fixed buffer of N bytes
    m = re.match(r"char\s+\w+\[(\d+)\]$", decl)
    if m:
        return f"char[{m.group(1)}]", int(m.group(1))

    # Strip array suffix (e.g. packet_[kHeaderBytes + ...]) → treat as pointer
    if re.search(r"\[\w", decl):
        return "array", 8  # can't evaluate constant expressions statically

    # Strip variable name (last token)
    tokens = decl.split()
    if len(tokens) < 2:
        return None, 0
    tokens = tokens[:-1]
    type_str = " ".join(tokens).strip()

    # Pointer/array → 8B on 64-bit
    if "*" in type_str:
        return type_str, 8

    # Template type like std::unique_ptr<...>
    m = re.match(r"([\w:]+)<.*>$", type_str)
    if m:
        outer = m.group(1)
        if outer in TYPE_SIZES:
            return outer, TYPE_SIZES[outer]
        return outer, 0  # unknown

    # Plain type (possibly qualified: std::string, uint8_t, etc.)
    if type_str in TYPE_SIZES:
        return type_str, TYPE_SIZES[type_str]

    # Strip namespace qualifier and retry
    bare = type_str.split("::")[-1]
    if bare in TYPE_SIZES:
        return bare, TYPE_SIZES[bare]

    return type_str, 0  # unknown


def _strip_inline_bodies(text: str) -> str:
    """Remove inline method bodies (brace-delimited blocks inside a class body).

    Replaces each {...} block that is not the class-opening brace with a
    single ';' so that local variable declarations inside methods don't look
    like field declarations.  Operates on already-comment-stripped text.
    """
    result = []
    i = 0
    n = len(text)
    # Track whether we are inside the class body (depth >= 1 after the first '{').
    # Depth 1 = class body; depth 2+ = inline method / nested struct body.
    class_depth = 0
    method_depth = 0
    skipping = False

    while i < n:
        ch = text[i]
        if ch == "{":
            class_depth += 1
            if class_depth == 1:
                # This is the class-opening brace — keep it.
                result.append(ch)
            else:
                method_depth += 1
                if method_depth == 1:
                    skipping = True  # start of inline method body
        elif ch == "}":
            if class_depth == 0:
                result.append(ch)
            elif method_depth > 0:
                method_depth -= 1
                if method_depth == 0:
                    skipping = False
                    result.append(";")  # placeholder so section splitting still works
            else:
                class_depth -= 1
                result.append(ch)
        else:
            if not skipping:
                result.append(ch)
        i += 1
    return "".join(result)


def _extract_own_fields(path: Path) -> tuple[list[tuple[str, str, int]], list[str]]:
    """Return (fields, unknown_types) from a single header.

    fields: [(field_name, type_str, size_bytes), ...]
    unknown_types: type strings we couldn't resolve
    """
    text = path.read_text(encoding="utf-8")
    # Remove line comments first, then block comments
    text_nc = re.sub(r"//[^\n]*", "", text)
    text_nc = re.sub(r"/\*.*?\*/", "", text_nc, flags=re.DOTALL)

    fields: list[tuple[str, str, int]] = []
    unknown: list[str] = []

    # Locate class body
    m = re.search(r"\bclass\b[^{;]*\{", text_nc, re.DOTALL)
    if not m:
        return fields, unknown
    body_start = m.end()

    # Brace-balance to find end
    depth = 1
    pos = body_start
    while pos < len(text_nc) and depth > 0:
        ch = text_nc[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        pos += 1
    class_body = text_nc[body_start:pos - 1]

    # Strip inline method bodies so local vars inside them don't look like fields
    class_body = _strip_inline_bodies(class_body)

    # Split into sections by access specifiers
    sections = re.split(r"\b(private|protected|public)\s*:", class_body)
    in_private = False
    for tok in sections:
        if tok in ("private", "protected"):
            in_private = True
            continue
        if tok == "public":
            in_private = False
            continue
        if not in_private:
            continue
        for line in tok.splitlines():
            line = line.strip()
            if not line or not line.endswith(";"):
                continue
            if "(" in line or ")" in line:
                continue  # method declaration (possibly a continuation line)
            if line.startswith("//") or line.startswith("*"):
                continue
            if re.match(r"\bstatic\b", line):
                continue
            if re.match(r"\bfriend\b", line):
                continue
            if re.match(r"\busing\b", line):
                continue
            stripped = line.rstrip(";").strip()
            if len(stripped.split()) < 2:
                continue
            type_str, size = _parse_type(line)
            if type_str is None:
                continue
            name = stripped.split()[-1].lstrip("*").split("[")[0]
            if size == 0:
                unknown.append(type_str)
            fields.append((name, type_str, size))

    return fields, unknown


def _moonmodule_subclasses() -> list[Path]:
    """Find all .h files under src/ that declare a class inheriting MoonModule."""
    results = []
    for f in (ROOT / "src").rglob("*.h"):
        text = f.read_text(encoding="utf-8")
        if re.search(r"\bclass\b[^{;]*\bMoonModule\b", text):
            results.append(f)
    return sorted(results)


# Types whose instances own heap memory beyond their stack/struct footprint.
# Values are the known fixed heap overhead on 64-bit; dynamic content is extra.
HEAP_MEMBER_OVERHEAD: dict[str, int] = {
    "std::string":  0,    # SSO: heap only when content > 15 chars (noted inline)
    "std::vector":  0,    # heap-backed; size unknown until runtime
    "std::map":     0,    # each node heap-allocated; size unknown until runtime
    "JsonDocument": 128,  # ArduinoJson 7 internal slab (fixed overhead)
}

# Regex patterns to detect dynamic allocation call sites in source.
# Each tuple: (label, pattern).  Match group 1 is the size expression.
_ALLOC_PATTERNS = [
    ("psram_alloc", re.compile(r"psram_alloc\(([^)]+)\)")),
    ("new[]",       re.compile(r"new\s+\w+\[([^\]]+)\]")),
    ("resize",      re.compile(r"\.resize\(([^)]+)\)")),
    ("reserve",     re.compile(r"\.reserve\(([^)]+)\)")),
]


def _heap_lines(path: Path) -> tuple[list[str], list[str]]:
    """Return (heap_member_notes, dynamic_alloc_exprs) for a header file.

    heap_member_notes: one string per heap-backed member field, e.g.
        "JsonDocument doc (128B slab)"  "std::string body (heap if >15 chars)"
    dynamic_alloc_exprs: one string per allocation call site, e.g.
        "psram_alloc(want_pixels * sizeof(RGB))"
    """
    text = path.read_text(encoding="utf-8")
    text_nc = re.sub(r"//[^\n]*", "", text)
    text_nc = re.sub(r"/\*.*?\*/", "", text_nc, flags=re.DOTALL)

    # Heap members: scan private/protected fields for heap-backed types.
    # Re-use _extract_own_fields to get canonical field names + types.
    own_fields, _ = _extract_own_fields(path)
    heap_notes = []
    for name, type_str, _size in own_fields:
        canonical = type_str.split("::")[-1] if "::" in type_str else type_str
        # Also match template outer, e.g. "std::vector" from "std::vector<uint8_t>"
        outer = re.match(r"([\w:]+)<", type_str)
        check_key = outer.group(1) if outer else type_str
        if check_key in HEAP_MEMBER_OVERHEAD:
            overhead = HEAP_MEMBER_OVERHEAD[check_key]
            if check_key == "std::string":
                heap_notes.append(f"{canonical} {name} (heap if content >15B)")
            elif check_key == "JsonDocument":
                heap_notes.append(f"JsonDocument {name} ({overhead}B fixed slab)")
            else:
                heap_notes.append(f"{canonical} {name} (runtime-sized)")

    # Dynamic allocations: scan the full file for alloc call sites,
    # but only inside method bodies (i.e. inside braces), not in comments.
    alloc_exprs = []
    for label, pat in _ALLOC_PATTERNS:
        for m in pat.finditer(text_nc):
            expr = m.group(1).strip()
            # Collapse whitespace for readability
            expr = re.sub(r"\s+", " ", expr)
            alloc_exprs.append(f"{label}({expr})")

    return heap_notes, alloc_exprs


def _canonical(type_str: str) -> str:
    """Collapse pointer variants and char arrays to a display-friendly bucket name."""
    if type_str.startswith("char["):
        return "char[]"
    if "*" in type_str or type_str == "array":
        return "pointer"
    return type_str


def main(_argv):
    any_unknown = False
    classes = _moonmodule_subclasses()

    verbose = "--verbose" in sys.argv or "-v" in sys.argv

    base_buckets: dict[str, list[int]] = {}
    for _name, size in MOONMODULE_BASE_FIELDS:
        key = _canonical("pointer" if size == 8 and _name.endswith("_") else
                         "std::string" if size == 24 else
                         f"uint{size*8}_t" if size in (1, 2, 4) else
                         "int64_t" if size == 8 and not _name.endswith("_") else
                         "pointer")
        base_buckets.setdefault(key, []).append(size)
    base_breakdown = "  " + " | ".join(
        f"{key}×{len(base_buckets[key])}={sum(base_buckets[key])}B"
        for key in sorted(base_buckets)
    )
    print(f"     {'MoonModule (base)':<30}  own={MOONMODULE_BASE:>4} B  total={MOONMODULE_BASE:>4} B{base_breakdown}")
    print()

    for path in classes:
        class_m = re.search(r"\bclass\s+(\w+)\b", path.read_text(encoding="utf-8"))
        class_name = class_m.group(1) if class_m else path.stem

        own_fields, unknown = _extract_own_fields(path)
        own_size = _layout_size([(n, s) for n, _t, s in own_fields if s > 0])
        estimated = MOONMODULE_BASE + own_size

        if unknown:
            any_unknown = True
            status = "WARN"
            note = f"  unknown: {', '.join(sorted(set(unknown)))}"
        else:
            status = "OK  "
            note = ""

        # Per-type breakdown: group fields by canonical type bucket
        buckets: dict[str, list[int]] = {}
        for _name, type_str, size in own_fields:
            key = _canonical(type_str)
            buckets.setdefault(key, []).append(size)

        breakdown = "  " + " | ".join(
            f"{key}×{len(buckets[key])}={sum(buckets[key])}B"
            for key in sorted(buckets)
        ) if buckets else ""

        heap_notes, alloc_exprs = _heap_lines(path)

        print(f"{status} {class_name:<30}  own={own_size:>4} B  total={estimated:>4} B{note}{breakdown}")
        indent = " " * 63
        if heap_notes:
            print(f"{indent}heap:  {', '.join(heap_notes)}")
        if alloc_exprs:
            print(f"{indent}alloc: {', '.join(alloc_exprs)}")

        if verbose:
            for name, type_str, size in own_fields:
                flag = "?" if size == 0 else " "
                print(f"         {flag} {type_str:<28} {name:<28} {size:>4} B")
            print()

    if any_unknown:
        print("FAIL: unknown types found — add them to TYPE_SIZES in scripts/check_class_sizes.py")
        return 1
    print("OK   all types resolved")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
