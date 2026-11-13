#!/usr/bin/env python3
"""FK-013 (U027): what the specifications claim exists, exists.

Checks the normative documents (PRD, RFC/ADR, architecture, API spec, contract, test plan, acceptance matrix,
traceability, UI reference, format docs):

1. Test references in backticks — a test program (`test_export`), a test (`Export.SourceFileUntouched`,
   `CacheLedger.*`), a self-test section (`runLayout`, `scenarioMemory`, `--scenario text`) or a file under
   tests/, tools/, core/, app/, cmake/, .github/, licenses/ — must name something in the repository.
2. Every name declared in the API spec's code blocks (functions, types, enum values, constants) appears in the
   public headers core/include/fk/*.hpp, and every header the spec names exists.

A reference that is not built yet is allowed when its line says so: "planned", "not built", "not implemented",
"never existed", "never implemented", "计划", "未实现", "不存在", "尚未". Historical records (WORKLOG, CHANGELOG,
release notes) are not checked: they describe what was true at the time.

Usage: tools/check-claims.py [--list]   (exit 1 when a claim has nothing behind it)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = (
    sorted((ROOT / "PRD").glob("*.md"))
    + sorted((ROOT / "rfc").glob("*.md"))
    + [ROOT / n for n in ("ARCHITECTURE.md", "API-SPEC-core.md", "CONTRACT-ui-core.md", "TEST-PLAN.md",
                          "PERFORMANCE-BUDGET.md", "SECURITY-PRIVACY.md", "RELEASE-CHECKLIST.md",
                          "THIRD-PARTY-LICENSES.md", "licenses/README.md")]
    + [ROOT / "docs" / n for n in ("acceptance-matrix.md", "ui-reference.md", "project-format.md",
                                   "color-pipeline.md", "film-types.md", "mobile-portability.md",
                                   "HUMAN-VERIFICATION.md", "INSTALL.md")]
    + [ROOT / "docs" / "v1" / n for n in ("TRACEABILITY.md", "FORMAT-MATRIX.md", "FEATURE-FREEZE.md",
                                          "REAL-SCAN-GT.md", "UNITS.md")]
)
PLANNED = re.compile(r"planned|not built|never built|not implemented|never existed|never implemented|removed|计划|未实现|不存在|尚未|已移除",
                     re.IGNORECASE)
TICK = re.compile(r"`([^`\n]+)`")


def collect_tests():
    programs, suites, cases = {}, set(), set()
    for f in (ROOT / "tests").rglob("*.cpp"):
        names = programs.setdefault(f.stem, set())
        for m in re.finditer(r"\bTEST(?:_F|_P)?\(\s*(\w+)\s*,\s*(\w+)\s*\)", f.read_text(encoding="utf-8")):
            suites.add(m.group(1))
            cases.add(f"{m.group(1)}.{m.group(2)}")
            names |= {m.group(1), m.group(2)}
    selftest = set()
    for f in (ROOT / "app" / "src").glob("SelfTest.*"):
        selftest |= set(re.findall(r"\b((?:run|scenario)[A-Z]\w*)\s*\(", f.read_text(encoding="utf-8")))
    scenarios = set(re.findall(r'name\s*==\s*QLatin1String\(\s*"(\w+)"\s*\)',
                               (ROOT / "app" / "src" / "SelfTest.cpp").read_text(encoding="utf-8")))
    return programs, suites, cases, selftest, scenarios


def check_references(problems):
    programs, suites, cases, selftest, scenarios = collect_tests()
    workflows = "\n".join(p.read_text(encoding="utf-8") for p in (ROOT / ".github" / "workflows").glob("*.yml"))
    for doc in DOCS:
        if not doc.exists():
            continue
        rel = doc.relative_to(ROOT)
        for n, line in enumerate(doc.read_text(encoding="utf-8").splitlines(), 1):
            if PLANNED.search(line):
                continue
            for raw in TICK.findall(line):
                ref = raw.strip()
                why = None
                path_ref = re.match(r"^(tests|tools|core|app|cmake|\.github|licenses)/[\w./*-]*$", ref)
                if path_ref:
                    target = ref.rstrip("/")
                    if "*" in target:
                        if not list(ROOT.glob(target)):
                            why = "no such files"
                    elif not (ROOT / target).exists():
                        why = "no such file"
                elif re.fullmatch(r"(?:test|tst)_\w+(?:\.cpp)?", ref):
                    if ref.removesuffix(".cpp") not in programs:
                        why = "no such test program"
                elif re.fullmatch(r"(?:test|tst)_\w+\.\w+", ref):
                    # `test_project.RoundTrip`: the program, and a suite or test of that name in it (the first
                    # version checked only the program, and PRD F11 named four tests that never existed)
                    program, case = ref.split(".", 1)
                    if program not in programs:
                        why = "no such test program"
                    elif case not in programs[program]:
                        why = f"no suite or test {case} in {program}"
                elif m := re.fullmatch(r"([A-Z]\w+)\.(\*|[A-Z]\w*)", ref):
                    suite, case = m.groups()
                    # only suites that look like gtest suites (a known one, or a CamelCase.CamelCase pair)
                    if suite in suites:
                        if case != "*" and f"{suite}.{case}" not in cases:
                            why = "no such test"
                    elif not re.fullmatch(r"(Qt|QQuick|Q[A-Z])\w*", suite) and (
                            case == "*" or (re.fullmatch(r"[A-Z][a-z]\w*", case) and len(case) > 12)):
                        why = "no such test suite"
                elif re.fullmatch(r"(?:run|scenario)[A-Z]\w*", ref):
                    if ref not in selftest:
                        why = "no such self-test section"
                elif m := re.fullmatch(r"--scenario (\w+)", ref):
                    if m.group(1) not in scenarios:
                        why = "no such scenario"
                if why:
                    problems.append(f"{rel}:{n}: `{ref}` — {why}")
            # a CI job named in the text runs in a workflow (FK-013: "mobile-lite job" did not exist)
            for job in re.findall(r"\bjob `([\w-]+)`", line):
                if not re.search(r"(?m)^\s+" + re.escape(job) + r":\s*$|name:\s*" + re.escape(job) + r"\b", workflows):
                    problems.append(f"{rel}:{n}: CI job `{job}` — not in .github/workflows")


def check_api_spec(problems):
    spec = (ROOT / "API-SPEC-core.md").read_text(encoding="utf-8")
    headers = "\n".join(p.read_text(encoding="utf-8") for p in (ROOT / "core" / "include" / "fk").glob("*.h*"))
    # string codes (reason codes, schema names) and the library target live in the sources
    core = headers + "\n".join(p.read_text(encoding="utf-8") for p in (ROOT / "core").rglob("*")
                                if p.is_file() and p.suffix in (".cpp", ".hpp", ".txt", ".cmake"))
    for h in sorted(set(re.findall(r"`fk/(\w+\.h(?:pp)?)`", spec))):
        if not (ROOT / "core" / "include" / "fk" / h).exists() and not PLANNED.search(
                next((l for l in spec.splitlines() if f"fk/{h}" in l), "")):
            problems.append(f"API-SPEC-core.md: header fk/{h} — no such header")
    in_block, lang, planned_section = False, "", False
    for n, line in enumerate(spec.splitlines(), 1):
        if line.startswith("## "):
            planned_section = bool(PLANNED.search(line))
        if line.startswith("```"):
            in_block = not in_block
            lang = line[3:].strip()
            continue
        if planned_section:
            continue
        if not in_block:
            # prose: backticked identifiers and "Name {a, b}" lists name types and values of the API
            if PLANNED.search(line):
                continue
            for raw in TICK.findall(line):
                if "/" in raw or raw.endswith((".md", ".cpp", ".hpp", ".h")):
                    continue  # a path: checked as a reference
                for name in re.findall(r"\b([A-Za-z_]\w{2,})\b", raw.split("(")[0]):
                    if name in ("const", "path", "void", "int", "bool", "double", "std", "string", "uint64_t",
                                "full", "fk", "hpp", "json", "project", "sidecar", "template", "delta",
                                "applyUpright", "Result", "OpenMode"):
                        continue
                    if re.fullmatch(r"fk\.\w+|[a-z]+", name) and "_" not in name and not name.islower():
                        continue
                    if not re.search(r"\b" + re.escape(name) + r"\b", core):
                        problems.append(f"API-SPEC-core.md:{n}: `{name}` — not in core/")
            continue
        if lang not in ("cpp", "c", ""):
            continue
        code = line.split("//")[0].split("/*")[0]
        names = set()
        names |= set(re.findall(r"\b(?:struct|class|enum class|enum)\s+(\w+)", code))
        names |= set(re.findall(r"\b(\w+)\s*\(", code)) - {"if", "for", "while", "return", "sizeof", "decltype",
                                                          "operator", "Result", "max", "min"}
        names |= set(re.findall(r"constexpr\s+\w+\s+(\w+)", code))
        names |= {m for m in re.findall(r"\b([a-z][a-z0-9_]+)\s*=\s*\d+", code)}
        for name in sorted(names):
            if len(name) < 3 or name in ("const", "virtual", "public", "inline", "template"):
                continue
            if not re.search(r"\b" + re.escape(name) + r"\b", headers):
                problems.append(f"API-SPEC-core.md:{n}: `{name}` — not in core/include/fk")


def main() -> int:
    problems = []
    check_references(problems)
    check_api_spec(problems)
    for p in problems:
        print("claim without code:", p)
    print(f"check-claims: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
