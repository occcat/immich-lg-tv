#!/usr/bin/env python3
"""Check project identity and the manual-only CI contract before publication."""
import json
import re
from project import ROOT, NAME, TITLE, APP, VERSION


def main():
    assert NAME == "immich-lg-tv" and TITLE == "Immich LG TV"
    core = (ROOT / "src/core.h").read_text()
    assert f'#define APP_ID "{APP}"' in core and f'#define APP_VERSION "{VERSION}"' in core
    for workflow in (ROOT / ".github/workflows").glob("*.y*ml"):
        content = workflow.read_text()
        event = re.search(r'(?m)^["\']?on["\']?:\s*\n((?:[ \t]+.*\n|\n)+)', content)
        assert event, f"Missing explicit event block: {workflow}"
        assert re.findall(r"(?m)^  ([\w-]+):", event.group(1)) == ["workflow_dispatch"], workflow
        assert not re.search(r"(?m)^  (push|pull_request|pull_request_target|schedule|workflow_call|workflow_run):", content), workflow
        assert "contents: read" in content
        assert all(re.fullmatch(r"[0-9a-f]{40}", ref) for ref in re.findall(r"uses: [^@\s]+@([^\s]+)", content))
    labels = json.loads((ROOT / ".github/labels.json").read_text())
    assert len({label["name"] for label in labels}) == len(labels)
    assert all(re.fullmatch(r"[a-f0-9]{6}", label["color"]) and label["description"] for label in labels)
    readme = (ROOT / "README.md").read_text()
    assert TITLE in readme and "Quick Start" in readme
    for image in re.findall(r'(?:src="|!\[[^\]]*\]\()((?:docs|assets)/[^"\s)]+)', readme):
        assert (ROOT / image).is_file(), image
    assert "Every commit message MUST" in (ROOT / "AGENTS.md").read_text()
    print("Project identity, screenshot links, labels, and manual-only CI verified.")


if __name__ == "__main__": main()
