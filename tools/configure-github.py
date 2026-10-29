#!/usr/bin/env python3
"""Apply this repository's description, topics, and issue labels manually."""
import json
import subprocess
from urllib.parse import quote
from project import ROOT, DESCRIPTION, REPOSITORY, PROJECT


def api(method, path, data=None):
    args = ["gh", "api", "--method", method, path]
    if data is not None: args += ["--input", "-"]
    result = subprocess.run(args, input=json.dumps(data) if data is not None else None,
                            text=True, capture_output=True, check=True)
    return json.loads(result.stdout) if result.stdout.strip() else None


def main():
    repo = api("GET", f"repos/{REPOSITORY}")
    expected = PROJECT.get("visibility", "private")
    if repo["private"] != (expected == "private"):
        raise SystemExit(f"Repository visibility does not match project.json ({expected}).")
    api("PATCH", f"repos/{REPOSITORY}", {"description": DESCRIPTION})
    topics = json.loads((ROOT / ".github/topics.json").read_text())
    api("PUT", f"repos/{REPOSITORY}/topics", {"names": topics})
    existing = {label["name"] for label in api("GET", f"repos/{REPOSITORY}/labels?per_page=100")}
    labels = json.loads((ROOT / ".github/labels.json").read_text())
    for label in labels:
        path = f"repos/{REPOSITORY}/labels"
        method = "POST"
        if label["name"] in existing:
            path += "/" + quote(label["name"], safe="")
            method = "PATCH"
        api(method, path, label)
    print(f"Configured {REPOSITORY}: {expected}, description, {len(topics)} topics, {len(labels)} project labels.")


if __name__ == "__main__": main()
