#!/usr/bin/env python3
"""Repository-level validation that does not require the .NET SDK.

This is intentionally not a replacement for `dotnet build` or the Windows render
smoke test. It catches malformed metadata, broken references, text corruption,
unbalanced C# delimiters, and inconsistent package configuration before those
steps run in CI.
"""
from __future__ import annotations

import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
ERRORS: list[str] = []
WARNINGS: list[str] = []


def fail(message: str) -> None:
    ERRORS.append(message)


def warn(message: str) -> None:
    WARNINGS.append(message)


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def all_files() -> Iterable[Path]:
    ignored = {".git", "bin", "obj", "artifacts", ".vs"}
    for path in ROOT.rglob("*"):
        if path.is_file() and not any(part in ignored for part in path.parts):
            yield path


def validate_required_files() -> None:
    required = [
        "AetherUI.sln",
        "global.json",
        "Directory.Build.props",
        "Directory.Packages.props",
        "README.md",
        "LICENSE",
        "SECURITY.md",
        "src/AetherUI.Core/AetherUI.Core.csproj",
        "src/AetherUI.Windows/AetherUI.Windows.csproj",
        "src/AetherUI.Generator/AetherUI.Generator.csproj",
        "src/AetherUI.Generator/buildTransitive/AetherUI.Generator.props",
        "samples/RegistrationApp/Views/RegistrationView.cshtml",
        "tests/AetherUI.Tests/AetherUI.Tests.csproj",
    ]
    for item in required:
        if not (ROOT / item).is_file():
            fail(f"Falta el archivo requerido: {item}")


def validate_text_files() -> None:
    binary_suffixes = {".png", ".jpg", ".jpeg", ".gif", ".ico", ".dll", ".exe", ".zip", ".nupkg", ".snupkg"}
    for path in all_files():
        if path.suffix.lower() in binary_suffixes:
            continue
        try:
            data = path.read_bytes()
            text = data.decode("utf-8-sig")
        except UnicodeDecodeError as exc:
            fail(f"{rel(path)} no es UTF-8: {exc}")
            continue
        if re.search(r"(?m)^(?:<<<<<<<|=======|>>>>>>>)", text):
            fail(f"{rel(path)} contiene marcadores de conflicto")
        if text and not text.endswith(("\n", "\r")):
            fail(f"{rel(path)} no termina en salto de línea")
        for number, line in enumerate(text.splitlines(), 1):
            if line.rstrip(" \t") != line:
                fail(f"{rel(path)}:{number} contiene espacios finales")


def validate_json_yaml_xml() -> None:
    for path in ROOT.rglob("*.json"):
        if any(part in {"bin", "obj", "artifacts"} for part in path.parts):
            continue
        try:
            json.loads(path.read_text(encoding="utf-8-sig"))
        except Exception as exc:  # noqa: BLE001
            fail(f"JSON inválido en {rel(path)}: {exc}")

    for suffix in ("*.yml", "*.yaml"):
        for path in ROOT.rglob(suffix):
            text = path.read_text(encoding="utf-8-sig")
            if "\t" in text:
                fail(f"YAML contiene tabulaciones en {rel(path)}")
            if path.name in {"ci.yml", "ci.yaml"} and not all(token in text for token in ("name:", "jobs:", "runs-on:")):
                fail(f"Workflow CI incompleto en {rel(path)}")

    patterns = ("*.csproj", "*.props", "*.targets", "*.manifest", "*.xml")
    seen: set[Path] = set()
    for pattern in patterns:
        for path in ROOT.rglob(pattern):
            if path in seen or any(part in {"bin", "obj", "artifacts"} for part in path.parts):
                continue
            seen.add(path)
            try:
                ET.parse(path)
            except Exception as exc:  # noqa: BLE001
                fail(f"XML inválido en {rel(path)}: {exc}")


def validate_solution_and_projects() -> None:
    sln = ROOT / "AetherUI.sln"
    if not sln.exists():
        return
    text = sln.read_text(encoding="utf-8-sig")
    project_paths = re.findall(r'^Project\("[^"]+"\)\s*=\s*"[^"]+",\s*"([^"]+\.csproj)"', text, re.MULTILINE)
    if not project_paths:
        fail("AetherUI.sln no contiene proyectos .csproj reconocibles")
    for project in project_paths:
        path = ROOT / Path(project.replace("\\", "/"))
        if not path.is_file():
            fail(f"La solución referencia un proyecto inexistente: {project}")

    package_versions_path = ROOT / "Directory.Packages.props"
    versions: set[str] = set()
    if package_versions_path.exists():
        root = ET.parse(package_versions_path).getroot()
        versions = {item.attrib.get("Include", "") for item in root.iter("PackageVersion")}

    for project in ROOT.rglob("*.csproj"):
        if any(part in {"bin", "obj", "artifacts"} for part in project.parts):
            continue
        root = ET.parse(project).getroot()
        frameworks = [node.text.strip() for node in root.iter() if node.tag in {"TargetFramework", "TargetFrameworks"} and node.text]
        if not frameworks:
            fail(f"{rel(project)} no declara TargetFramework/TargetFrameworks")
        for reference in root.iter("ProjectReference"):
            include = reference.attrib.get("Include")
            if not include or "$" in include or "*" in include:
                continue
            target = (project.parent / include.replace("\\", "/")).resolve()
            if not target.is_file():
                fail(f"{rel(project)} referencia un proyecto inexistente: {include}")
        for reference in root.iter("PackageReference"):
            include = reference.attrib.get("Include", "")
            has_inline_version = bool(reference.attrib.get("Version")) or any(child.tag == "Version" for child in reference)
            if include and not has_inline_version and include not in versions:
                fail(f"{rel(project)} usa {include} sin versión central ni inline")


def validate_markdown_links() -> None:
    link_pattern = re.compile(r"(?<!!)\[[^\]]*\]\(([^)]+)\)")
    for path in ROOT.rglob("*.md"):
        text = path.read_text(encoding="utf-8-sig")
        for target in link_pattern.findall(text):
            target = target.strip().split(maxsplit=1)[0].strip("<>")
            if not target or target.startswith(("#", "http://", "https://", "mailto:")):
                continue
            target = target.split("#", 1)[0]
            candidate = (path.parent / target).resolve()
            try:
                candidate.relative_to(ROOT)
            except ValueError:
                fail(f"{rel(path)} enlaza fuera del repositorio: {target}")
                continue
            if not candidate.exists():
                fail(f"Enlace roto en {rel(path)}: {target}")


def count_run(text: str, index: int, value: str) -> int:
    count = 0
    while index + count < len(text) and text[index + count] == value:
        count += 1
    return count


def skip_block_comment(text: str, index: int) -> int:
    end = text.find("*/", index + 2)
    return len(text) if end < 0 else end + 2


def skip_line_comment(text: str, index: int) -> int:
    end = text.find("\n", index + 2)
    return len(text) if end < 0 else end


def skip_character_literal(text: str, index: int) -> int:
    cursor = index + 1
    while cursor < len(text):
        if text[cursor] == "\\":
            cursor += 2
        elif text[cursor] == "'":
            return cursor + 1
        else:
            cursor += 1
    return len(text)


def skip_raw_string(text: str, start: int, dollar_count: int, quote_count: int) -> int:
    cursor = start + dollar_count + quote_count
    while cursor < len(text):
        if text[cursor] == '"' and count_run(text, cursor, '"') >= quote_count:
            return cursor + quote_count
        cursor += 1
    return len(text)


def detect_string(text: str, index: int) -> tuple[int, bool, bool] | None:
    """Return (quote_index, verbatim, interpolated) for a regular C# string."""
    candidates = (
        ('$@"', True, True),
        ('@$"', True, True),
        ('@"', True, False),
        ('$"', False, True),
        ('"', False, False),
    )
    for prefix, verbatim, interpolated in candidates:
        if text.startswith(prefix, index):
            return index + len(prefix) - 1, verbatim, interpolated
    return None


def skip_regular_string(text: str, start: int, quote_index: int, verbatim: bool, interpolated: bool) -> int:
    cursor = quote_index + 1
    expression_depth = 0
    while cursor < len(text):
        if expression_depth == 0:
            if verbatim and text.startswith('""', cursor):
                cursor += 2
                continue
            if not verbatim and text[cursor] == "\\":
                cursor += 2
                continue
            if text[cursor] == '"':
                return cursor + 1
            if interpolated and text.startswith('{{', cursor):
                cursor += 2
                continue
            if interpolated and text.startswith('}}', cursor):
                cursor += 2
                continue
            if interpolated and text[cursor] == '{':
                expression_depth = 1
                cursor += 1
                continue
            cursor += 1
            continue

        skipped = skip_csharp_token(text, cursor)
        if skipped is not None:
            cursor = skipped
            continue
        if text[cursor] == '{':
            expression_depth += 1
        elif text[cursor] == '}':
            expression_depth -= 1
        cursor += 1
    return len(text)


def skip_csharp_token(text: str, index: int) -> int | None:
    if text.startswith('//', index):
        return skip_line_comment(text, index)
    if text.startswith('/*', index):
        return skip_block_comment(text, index)
    if text[index] == "'":
        return skip_character_literal(text, index)

    dollar_count = count_run(text, index, '$') if text[index] == '$' else 0
    quote_index = index + dollar_count
    if quote_index < len(text) and text[quote_index] == '"':
        quote_count = count_run(text, quote_index, '"')
        if quote_count >= 3:
            return skip_raw_string(text, index, dollar_count, quote_count)

    regular = detect_string(text, index)
    if regular is not None:
        quote, verbatim, interpolated = regular
        return skip_regular_string(text, index, quote, verbatim, interpolated)
    return None


def validate_csharp_delimiters() -> None:
    pairs = {')': '(', ']': '[', '}': '{'}
    openings = set(pairs.values())
    for path in ROOT.rglob('*.cs'):
        if any(part in {'bin', 'obj', 'artifacts'} for part in path.parts):
            continue
        text = path.read_text(encoding='utf-8-sig')
        stack: list[tuple[str, int]] = []
        index = 0
        reported = False
        while index < len(text):
            skipped = skip_csharp_token(text, index)
            if skipped is not None:
                index = skipped
                continue
            char = text[index]
            if char in openings:
                stack.append((char, index))
            elif char in pairs:
                if not stack or stack[-1][0] != pairs[char]:
                    fail(f"Delimitador inesperado '{char}' en {rel(path)} (offset {index})")
                    reported = True
                    break
                stack.pop()
            index += 1
        if stack and not reported:
            char, position = stack[-1]
            fail(f"Delimitador '{char}' sin cierre en {rel(path)} (offset {position})")

def skip_csharp_string_or_comment(text: str, index: int) -> int | None:
    if text.startswith("//", index):
        end = text.find("\n", index + 2)
        return len(text) if end < 0 else end
    if text.startswith("/*", index):
        end = text.find("*/", index + 2)
        return len(text) if end < 0 else end + 2
    if text[index] == "'":
        cursor = index + 1
        while cursor < len(text):
            if text[cursor] == "\\":
                cursor += 2
            elif text[cursor] == "'":
                return cursor + 1
            else:
                cursor += 1
        return len(text)
    if text[index] == '"':
        run = 1
        while index + run < len(text) and text[index + run] == '"':
            run += 1
        if run >= 3:
            end = text.find('"' * run, index + run)
            return len(text) if end < 0 else end + run
        verbatim = index > 0 and text[index - 1] == "@" or index > 1 and text[index - 2:index] == "@$"
        cursor = index + 1
        while cursor < len(text):
            if verbatim and text.startswith('""', cursor):
                cursor += 2
            elif not verbatim and text[cursor] == "\\":
                cursor += 2
            elif text[cursor] == '"':
                return cursor + 1
            else:
                cursor += 1
        return len(text)
    return None


def validate_cshtml_code_blocks() -> None:
    directive = re.compile(r"(?m)^[ \t]*@code(?=\s|\{)")
    for path in ROOT.rglob("*.cshtml"):
        text = path.read_text(encoding="utf-8-sig")
        for match in directive.finditer(text):
            cursor = match.end()
            while cursor < len(text) and text[cursor].isspace():
                cursor += 1
            if cursor >= len(text) or text[cursor] != "{":
                fail(f"{rel(path)} tiene @code sin bloque")
                continue
            depth = 0
            index = cursor
            while index < len(text):
                skipped = skip_csharp_string_or_comment(text, index)
                if skipped is not None:
                    index = skipped
                    continue
                if text[index] == "{":
                    depth += 1
                elif text[index] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                index += 1
            if depth != 0:
                fail(f"{rel(path)} tiene un bloque @code sin cierre")


def validate_product_invariants() -> None:
    forbidden = re.compile(r"System\.Windows\.Forms|Microsoft\.Web\.WebView2|CefSharp|Chromium", re.IGNORECASE)
    for path in list(ROOT.rglob("*.cs")) + list(ROOT.rglob("*.csproj")):
        text = path.read_text(encoding="utf-8-sig")
        if forbidden.search(text):
            fail(f"Dependencia de UI prohibida detectada en {rel(path)}")

    global_json = json.loads((ROOT / "global.json").read_text(encoding="utf-8-sig"))
    sdk_version = global_json.get("sdk", {}).get("version")
    if sdk_version != "10.0.302":
        fail(f"global.json debe fijar 10.0.302; actual: {sdk_version!r}")

    build_props = (ROOT / "Directory.Build.props").read_text(encoding="utf-8-sig")
    if "0.1.0-alpha.1" not in build_props:
        fail("Directory.Build.props no contiene la versión 0.1.0-alpha.1")

    generator_props = ROOT / "src/AetherUI.Generator/buildTransitive/AetherUI.Generator.props"
    if generator_props.exists() and "*.cshtml" not in generator_props.read_text(encoding="utf-8-sig"):
        fail("El paquete del generador no agrega archivos .cshtml")

    sample = ROOT / "samples/RegistrationApp/Views/RegistrationView.cshtml"
    if sample.exists():
        sample_text = sample.read_text(encoding="utf-8-sig")
        for expected in ("@code", "onsubmit=\"@EnviarFormulario\"", "oninput=\"@MostrarEscritura\""):
            if expected not in sample_text:
                fail(f"La demo no contiene la construcción esperada: {expected}")


def main() -> int:
    checks = [
        validate_required_files,
        validate_text_files,
        validate_json_yaml_xml,
        validate_solution_and_projects,
        validate_markdown_links,
        validate_csharp_delimiters,
        validate_cshtml_code_blocks,
        validate_product_invariants,
    ]
    for check in checks:
        try:
            check()
        except Exception as exc:  # noqa: BLE001
            fail(f"La comprobación {check.__name__} produjo una excepción: {exc}")

    for message in WARNINGS:
        print(f"WARN  {message}")
    if ERRORS:
        for message in ERRORS:
            print(f"FAIL  {message}")
        print(f"\nValidación estática fallida: {len(ERRORS)} error(es).")
        return 1
    print(f"PASS  Validación estática completada ({len(checks)} grupos de comprobaciones).")
    print("NOTE  Esta validación no sustituye dotnet build, las pruebas C# ni el smoke test Direct2D en Windows.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
