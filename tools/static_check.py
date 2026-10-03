#!/usr/bin/env python3
"""Lightweight consistency checker for the McServerManager C++ sources.

It catches the errors that a compiler would otherwise report first:
  * unresolved project local #include paths
  * unbalanced braces / parentheses
  * member functions declared in a header but never defined in the .cpp
  * Q_OBJECT classes without a matching .cpp
  * member variables (m_*) used in a .cpp but never declared in its header

Usage: python tools/static_check.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = [ROOT / "backend" / "src", ROOT / "frontend" / "src"]
INCLUDE_ROOTS = [ROOT / "backend" / "src", ROOT / "frontend" / "src"]

INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"', re.MULTILINE)
INLINE_FUNCTION_RE = re.compile(r"\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?\{")

errors: list[str] = []
warnings: list[str] = []


def strip_comments_and_strings(text: str) -> str:
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                i += 1
        elif ch == "/" and nxt == "*":
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                i += 1
            i += 2
        elif ch in ('"', "'"):
            quote = ch
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def check_includes(path: Path, text: str) -> None:
    for match in INCLUDE_RE.finditer(text):
        relative = match.group(1)
        candidates = [root / relative for root in INCLUDE_ROOTS]
        candidates.append(path.parent / relative)
        if not any(candidate.exists() for candidate in candidates):
            errors.append(f"{path.relative_to(ROOT)}: unresolved include \"{relative}\"")


def check_balance(path: Path, text: str) -> None:
    clean = strip_comments_and_strings(text)
    pairs = {"}": "{", ")": "(", "]": "["}
    counters = {"{": 0, "(": 0, "[": 0}
    for ch in clean:
        if ch in counters:
            counters[ch] += 1
        elif ch in pairs:
            counters[pairs[ch]] -= 1
            if counters[pairs[ch]] < 0:
                errors.append(f"{path.relative_to(ROOT)}: unbalanced '{ch}'")
                return
    if any(value != 0 for value in counters.values()):
        pending = ", ".join(f"{k}:{v}" for k, v in counters.items() if v)
        errors.append(f"{path.relative_to(ROOT)}: unbalanced delimiters ({pending})")


HEADER_CLASS_RE = re.compile(
    r"class\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::[^{;]*)?\{", re.MULTILINE
)


def collect_declarations(header_text: str, class_name: str) -> list[str]:
    """Returns member function names declared in the class body (no inline body)."""
    start = header_text.find(f"class {class_name}")
    if start < 0:
        return []
    brace = header_text.find("{", start)
    if brace < 0:
        return []
    depth = 0
    end = len(header_text)
    for index in range(brace, len(header_text)):
        if header_text[index] == "{":
            depth += 1
        elif header_text[index] == "}":
            depth -= 1
            if depth == 0:
                end = index
                break

    body = header_text[brace + 1 : end]
    body = strip_comments_and_strings(body)
    # Qt signals must not be reported as "declared but never defined".
    kept_lines = []
    in_signals = False
    for line in body.splitlines():
        stripped = line.strip()
        if re.match(r"^(public|private|protected)\s*(slots)?\s*:", stripped):
            in_signals = False
            continue
        if stripped in {"signals:", "Q_SIGNALS:", "signals", "Q_SIGNALS"}:
            in_signals = True
            continue
        if not in_signals:
            kept_lines.append(line)
    body = "\n".join(kept_lines)
    names: list[str] = []
    for statement in body.split(";"):
        if "(" not in statement or ")" not in statement:
            continue
        if "=" in statement and "operator" not in statement:
            continue
        if statement.strip().startswith(("using", "typedef", "friend", "template")):
            continue
        if "Q_PROPERTY" in statement or "Q_OBJECT" in statement:
            continue
        if INLINE_FUNCTION_RE.search(statement):
            continue
        match = re.search(r"([A-Za-z_~][A-Za-z0-9_]*)\s*\(", statement)
        if not match:
            continue
        name = match.group(1)
        if name in {"if", "for", "while", "switch", "return", "sizeof", "static_assert"}:
            continue
        names.append(name)
    return names


def check_implementation(path: Path) -> None:
    if path.suffix != ".h":
        return
    text = path.read_text(encoding="utf-8", errors="ignore")
    classes = [m.group(1) for m in HEADER_CLASS_RE.finditer(text)]
    if not classes:
        return
    cpp = path.with_suffix(".cpp")
    if "Q_OBJECT" in text and not cpp.exists():
        errors.append(f"{path.relative_to(ROOT)}: Q_OBJECT class without {cpp.name}")
    if not cpp.exists():
        return
    cpp_text = cpp.read_text(encoding="utf-8", errors="ignore")
    for class_name in classes:
        for name in collect_declarations(text, class_name):
            # allow nested qualifiers, e.g. Outer::Nested::method
            pattern = re.compile(
                rf"\b{re.escape(class_name)}::(?:[A-Za-z_][A-Za-z0-9_]*::)?{re.escape(name)}\s*\("
            )
            if not pattern.search(cpp_text):
                warnings.append(
                    f"{path.relative_to(ROOT)}: {class_name}::{name} declared but not defined in {cpp.name}"
                )


MEMBER_DECL_RE = re.compile(r"\b(m_[A-Za-z0-9_]+)\b")
MEMBER_USE_RE = re.compile(r"\bm_[A-Za-z0-9_]+\b")


def check_members(path: Path) -> None:
    """Reports m_* identifiers used in a .cpp that its header never declares."""
    if path.suffix != ".cpp":
        return
    header = path.with_suffix(".h")
    if not header.exists():
        return
    header_text = strip_comments_and_strings(header.read_text(encoding="utf-8", errors="ignore"))
    declared = set(MEMBER_DECL_RE.findall(header_text))
    if not declared:
        return
    body = strip_comments_and_strings(path.read_text(encoding="utf-8", errors="ignore"))
    used = set(MEMBER_USE_RE.findall(body))
    unknown = sorted(used - declared)
    for name in unknown:
        errors.append(
            f"{path.relative_to(ROOT)}: uses {name} but {header.name} does not declare it"
        )


# Methods that only exist on one project widget. Calls on a variable whose type
# is known (member pointer or `= new X(...)` local) are validated against it.
EXCLUSIVE_METHODS = {
    "setGlyph": {"GradientButton"},
    "setGlowProgress": {"GradientButton"},
    "setOnText": {"ToggleSwitch"},
    "setKnobProgress": {"ToggleSwitch"},
    "setVariant": {"CardFrame"},
    "setRadius": {"CardFrame"},
    "setInteractive": {"CardFrame"},
    "setGlow": {"CardFrame"},
    "setIndicatorPos": {"SegmentedControl"},
    "setItems": {"SegmentedControl"},
    "setTone": {"Chip"},
    "setCompact": {"Chip", "GradientButton"},
    "setLogLines": {"LogView"},
    "setFilter": {"LogView"},
    "appendLine": {"LogView"},
    "setPulse": {"StatusDot"},
    "setDiameter": {"StatusDot"},
    "selectServer": {"ServerSelector"},
    "setPlaceholder": {"ServerSelector"},
}

MEMBER_DECL_TYPE_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_:]*)\s*\*+\s*(m_[A-Za-z0-9_]+)")
LOCAL_TYPE_RE = re.compile(
    r"(?:auto\s*\*|([A-Za-z_][A-Za-z0-9_:]*)\s*\*+)\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*new\s+([A-Za-z_:][A-Za-z0-9_:]*)"
)
CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*->\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def check_widget_methods(path: Path) -> None:
    if path.suffix != ".cpp":
        return
    header = path.with_suffix(".h")
    if not header.exists():
        return
    header_clean = strip_comments_and_strings(header.read_text(encoding="utf-8", errors="ignore"))
    types = {name: kind for kind, name in MEMBER_DECL_TYPE_RE.findall(header_clean)}
    body = strip_comments_and_strings(path.read_text(encoding="utf-8", errors="ignore"))
    for kind, name, _created in LOCAL_TYPE_RE.findall(body):
        if name:
            types.setdefault(name, kind)
        else:
            # `auto *x = new Foo(...)` - capture created type via a second pass
            pass
    for match in LOCAL_TYPE_RE.finditer(body):
        created = match.group(3)
        variable = match.group(2)
        types[variable] = created

    for receiver, method in CALL_RE.findall(body):
        expected = EXCLUSIVE_METHODS.get(method)
        if not expected:
            continue
        actual = types.get(receiver)
        if not actual:
            continue
        if actual.split("::")[-1] not in expected:
            errors.append(
                f"{path.relative_to(ROOT)}: {receiver}->{method}() called on {actual}, "
                f"but {method} belongs to {'/'.join(sorted(expected))}"
            )


def check_cmake_lists() -> None:
    """Every source file must be listed in its CMakeLists (AUTOMOC relies on it)."""
    for subdir in ("backend", "frontend"):
        cmake = ROOT / subdir / "CMakeLists.txt"
        if not cmake.exists():
            errors.append(f"missing {cmake.relative_to(ROOT)}")
            continue
        listed = {
            line.strip()
            for line in cmake.read_text(encoding="utf-8", errors="ignore").splitlines()
            if line.strip().startswith("src/") and line.strip().endswith((".cpp", ".h"))
        }
        for entry in sorted(listed):
            if not (ROOT / subdir / entry).exists():
                errors.append(f"{cmake.relative_to(ROOT)}: listed but missing -> {entry}")
        on_disk = {
            str(p.relative_to(ROOT / subdir)).replace("\\", "/")
            for p in (ROOT / subdir / "src").rglob("*")
            if p.suffix in {".cpp", ".h"}
        }
        for entry in sorted(on_disk - listed):
            errors.append(f"{subdir}/CMakeLists.txt: source not listed -> {entry}")


TYPE_DEF_RE = re.compile(r"\b(?:class|struct|enum(?:\s+class)?)\s+([A-Za-z_][A-Za-z0-9_]*)")
MEMBER_TYPE_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\*+\s*m_[A-Za-z0-9_]+\s*[:=]")

# QLatin1String/QLatin1Char treat the bytes as Latin-1, so a UTF-8 multibyte
# literal turns into mojibake ("·" becomes "Â·") at runtime.
LATIN1_LITERAL_RE = re.compile(r"QLatin1(?:String|Char)\s*\(\s*\"([^\"]*)\"")


def check_latin1_literals(path: Path, text: str) -> None:
    for match in LATIN1_LITERAL_RE.finditer(text):
        value = match.group(1)
        if any(ord(char) > 127 for char in value):
            line = text[: match.start()].count("\n") + 1
            errors.append(
                f"{path.relative_to(ROOT)}:{line}: QLatin1String(\"{value}\") contains non-ASCII text; "
                f"use QStringLiteral instead"
            )


def collect_header_types(path: Path, seen: set) -> set:
    """Types visible in a header: own definitions/forward decls + included project headers."""
    if path in seen or not path.exists():
        return set()
    seen.add(path)
    raw = path.read_text(encoding="utf-8", errors="ignore")
    text = strip_comments_and_strings(raw)
    types = set(TYPE_DEF_RE.findall(text))
    # include directives must be read from the raw text: the stripper removes
    # the quoted path itself.
    for match in INCLUDE_RE.finditer(raw):
        relative = match.group(1)
        for root in INCLUDE_ROOTS:
            candidate = root / relative
            if candidate.exists():
                types |= collect_header_types(candidate, seen)
                break
    return types


def check_member_types(path: Path) -> None:
    """A pointer member whose type is never declared in the header will not compile."""
    if path.suffix != ".h":
        return
    text = strip_comments_and_strings(path.read_text(encoding="utf-8", errors="ignore"))
    known = collect_header_types(path, set())
    for kind in MEMBER_TYPE_RE.findall(text):
        if kind.startswith("Q") and kind not in known:
            # Qt types are usually pulled in through included Qt headers we do not parse.
            continue
        if kind not in known:
            errors.append(
                f"{path.relative_to(ROOT)}: member of type {kind} but that type is not declared/included"
            )


def main() -> int:
    files = []
    for directory in SOURCE_DIRS:
        if not directory.exists():
            errors.append(f"missing source directory {directory}")
            continue
        files.extend(sorted(p for p in directory.rglob("*") if p.suffix in {".h", ".cpp"}))

    if not files:
        print("no source files found")
        return 1

    check_cmake_lists()

    for path in files:
        text = path.read_text(encoding="utf-8", errors="ignore")
        check_includes(path, text)
        check_balance(path, text)
        check_implementation(path)
        check_members(path)
        check_widget_methods(path)
        check_member_types(path)
        check_latin1_literals(path, text)

    print(f"checked {len(files)} files")
    for warning in warnings:
        print(f"WARN  {warning}")
    for error in errors:
        print(f"ERROR {error}")
    print(f"{len(errors)} error(s), {len(warnings)} warning(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
