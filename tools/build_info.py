#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write safely escaped build metadata, updating only when values change."""
import json
import os
from pathlib import Path

commit = os.environ["BUILD_COMMIT"]
tag = os.environ.get("BUILD_TAG", "")
if not commit:
    raise SystemExit("A Git commit is required to identify this build.")
content = (
    f"#define BUILD_COMMIT {json.dumps(commit, ensure_ascii=True)}\n"
    f"#define BUILD_TAG {json.dumps(tag, ensure_ascii=True)}\n"
)
path = Path("build/build_info.h")
if not path.exists() or path.read_text() != content:
    path.write_text(content)
