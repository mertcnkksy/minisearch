"""Summarize GCC JSON coverage for MiniSearch production sources and headers."""

import argparse
from collections import defaultdict
import gzip
import json
from pathlib import Path
import re
import subprocess
import tempfile


def collect(build, source, gcov):
    modules = defaultdict(lambda: defaultdict(dict))
    with tempfile.TemporaryDirectory(prefix="minisearch-gcov-") as scratch:
        for notes in sorted(build.rglob("*.gcno")):
            parts = notes.relative_to(build).parts
            if "_deps" in parts or not any(part.endswith(".dir") for part in parts):
                continue
            data = notes.with_suffix(".gcda")
            try:
                with data.open("rb") as stream:
                    if not stream.read(1):
                        raise SystemExit(f"Empty coverage data: {data}")
            except OSError as error:
                raise SystemExit(f"Cannot read coverage data {data}: {error.strerror}") from error
            result = subprocess.run(
                [gcov, "--json-format", "--branch-probabilities", str(notes)],
                cwd=scratch, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True
            )
            if result.returncode != 0 or result.stderr.strip():
                raise SystemExit(f"gcov could not process {notes}\n{result.stderr.strip()}")
            reports = list(Path(scratch).glob("*.gcov.json.gz"))
            if len(reports) != 1:
                raise SystemExit(f"Expected one gcov report for {notes}")
            with gzip.open(reports[0], "rt", encoding="utf-8") as stream:
                report = json.load(stream)
            reports[0].unlink()
            for file in report["files"]:
                path = (Path(report["current_working_directory"]) / file["file"]).resolve()
                if not path.is_relative_to(source):
                    continue
                relative = path.relative_to(source)
                parts = relative.parts
                if parts[:2] == ("include", "minisearch") and len(parts) > 3:
                    module = parts[2]
                elif parts[0] == "src":
                    module = parts[1] if len(parts) > 2 else relative.stem
                elif parts[:2] == ("include", "minisearch"):
                    module = relative.stem
                else:
                    continue
                metrics = modules[module]
                for function in file["functions"]:
                    key = (relative, function["name"], function["start_line"])
                    record(metrics["functions"], key, function["execution_count"])
                for line in file["lines"]:
                    key = (relative, line["line_number"])
                    record(metrics["lines"], key, line["count"])
                    for ordinal, branch in enumerate(line["branches"]):
                        edge = (*key, line.get("function_name", ""), ordinal)
                        record(metrics["branches"], edge, branch["count"])
    return modules


def record(metrics, key, count):
    metrics[key] = metrics.get(key, False) or count > 0


def summary(values):
    return (f"{sum(values.values())}/{len(values)} "
            f"({100 * sum(values.values()) / len(values):.1f}%)") if values else "-"


def report(build, source, gcov):
    modules = collect(build, source, gcov)
    if not any(data["lines"] for data in modules.values()):
        raise SystemExit("No instrumented MiniSearch sources were found")
    names = ("functions", "lines", "branches", "explicit throw lines")
    total = {name: {} for name in names}
    print("| Module | Functions | Lines | Taken branch edges | Explicit throw lines |")
    print("| --- | ---: | ---: | ---: | ---: |")
    for module, data in sorted(modules.items()):
        source_lines = {}
        for (path, line), covered in data["lines"].items():
            if path not in source_lines:
                source_lines[path] = (source / path).read_text(encoding="utf-8").splitlines()
            if re.search(r"\bthrow\b", source_lines[path][line - 1].split("//")[0]):
                record(data["explicit throw lines"], (path, line), covered)
        for name in names:
            total[name].update(data[name])
        print(f"| {module} | " + " | ".join(summary(data[name]) for name in names) + " |")
    print("| **Total** | " + " | ".join(summary(total[name]) for name in names) + " |")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--source", type=Path, default=Path.cwd())
    parser.add_argument("--gcov", default="gcov")
    options = parser.parse_args()
    report(options.build.resolve(), options.source.resolve(), options.gcov)
