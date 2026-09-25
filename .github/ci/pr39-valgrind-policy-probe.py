"""Temporary CI proof that Meson and the existing XML gate preserve failures."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


root = Path.cwd().resolve()
helpers = root / "scripts/testing/valgrind"
env = dict(os.environ, VALGRIND_OPTS="--error-exitcode=0")
with tempfile.TemporaryDirectory(prefix=".pr39-valgrind-policy-", dir=root) as tmp:
    work = Path(tmp)
    source = work / "source"
    source.mkdir()
    (source / "probe.c").write_text(
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "int main(int argc, char **argv) {\n"
        "  if (argc != 2) return 9;\n"
        "  if (!strcmp(argv[1], \"process_failure\")) return 7;\n"
        "  if (!strcmp(argv[1], \"memory_error\")) {\n"
        "    volatile int *p = malloc(sizeof(*p));\n"
        "    free((void *)p);\n"
        "    *p = 42;\n"
        "  }\n"
        "  return 0;\n"
        "}\n"
    )
    (source / "meson.build").write_text(
        "project('pr39-policy-probe', 'c', default_options: ['buildtype=debug'])\n"
        "probe = executable('probe', 'probe.c')\n"
        "foreach name : ['clean', 'process_failure', 'memory_error']\n"
        "  test(name, probe, args: [name])\n"
        "endforeach\n"
    )
    build = work / "build"
    subprocess.run(["meson", "setup", str(build), str(source)], check=True)
    subprocess.run(["meson", "compile", "-C", str(build)], check=True)
    for name, expected_meson, expected_process, expected_gate in (
        ("clean", 0, 0, 0),
        ("process_failure", 1, 7, 0),
        ("memory_error", 0, 0, 1),
    ):
        logs = work / name
        logs.mkdir()
        wrapper = f"valgrind --xml=yes --xml-file={logs}/probe.valgrind.%p.xml"
        result = subprocess.run(
            ["meson", "test", "-C", str(build), "--no-rebuild", "--wrapper", wrapper, name],
            env=env,
        )
        assert result.returncode == expected_meson, (name, result.returncode)
        report = json.loads((build / "meson-logs/testlog-valgrind.json").read_text())
        assert report["returncode"] == expected_process, (name, report["returncode"])
        summary = logs / "summary.xml"
        subprocess.run(
            [sys.executable, str(helpers / "summarize-valgrind-output.py"),
             "--logs-dir", str(logs), "--output", str(summary)], check=True,
        )
        summary.write_text(summary.read_text().replace(str(root), "/work"))
        gate = subprocess.run(
            [sys.executable, str(helpers / "compare-valgrind-results.py"),
             "--baseline", str(root / ".github/ci/baselines/valgrind-baseline.xml"),
             "--current", str(summary)],
        )
        assert gate.returncode == expected_gate, (name, gate.returncode)
        print(f"PROVED {name}: process={expected_process}, meson={expected_meson}, gate={expected_gate}", flush=True)
