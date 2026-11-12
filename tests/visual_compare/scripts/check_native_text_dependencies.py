#!/usr/bin/env python3
"""Read-only preflight for T1.1; does not install, build, or render anything."""
import json
import shutil
import subprocess
import sys

EXPECTED = {"harfbuzz": "15.1.0", "freetype2": "2.15.0"}


def inspect_dependencies():
    result = {"schema": "aether-native-text-preflight-v1", "render": "NOT_RUN",
              "installation_attempted": False, "dependencies": {}}
    pkg = shutil.which("pkg-config")
    ready = pkg is not None
    for package, expected in EXPECTED.items():
        item = {"expected_version": expected, "status": "MISSING"}
        if pkg:
            try:
                p = subprocess.run([pkg, "--modversion", package], capture_output=True,
                                   text=True, timeout=10, check=False)
                if p.returncode == 0:
                    actual = p.stdout.strip()
                    item.update(actual_version=actual,
                                status="MATCH" if actual == expected else "VERSION_MISMATCH")
            except (OSError, subprocess.TimeoutExpired):
                item["status"] = "CHECK_FAILED"
        ready = ready and item["status"] == "MATCH"
        result["dependencies"][package] = item
    result["pkg_config_available"] = pkg is not None
    # Metadata alone cannot prove headers/linkage, source integrity, font licensing,
    # or the real rendering gate. Never label this preflight a backend PASS.
    result["status"] = "METADATA_READY" if ready else "BLOCKED"
    result["remaining_gates"] = ["verified_dependency_hashes_and_notices", "licensed_font_hashes",
                                 "compile_and_link_real_dependencies", "native_text_implementation",
                                 "independent_html_to_png_validation"]
    return result


if __name__ == "__main__":
    report = inspect_dependencies()
    print(json.dumps(report, indent=2, sort_keys=True))
    sys.exit(0 if report["status"] == "METADATA_READY" else 2)
