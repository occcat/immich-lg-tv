"""Shared project and package identity for local tooling."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROJECT = json.loads((ROOT / "project.json").read_text())
NAME = PROJECT["name"]
TITLE = PROJECT["displayName"]
APP = PROJECT["applicationId"]
VERSION = PROJECT["version"]
DESCRIPTION = PROJECT["description"]
REPOSITORY = PROJECT["repository"]
PACKAGE = f"{APP}_{VERSION}_arm.ipk"
