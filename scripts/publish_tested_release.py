"""Publish the exact Windows package from a successful, explicitly selected build."""
import base64
import datetime
import email.utils
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

REPO = "ypqlmen/ProviTracker"
SPARKLE = "http://www.andymatuschak.org/xml-namespaces/sparkle"
PROVI = "https://provi-tracker.local/update"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def api(path, data=None):
    request = urllib.request.Request(
        f"https://api.github.com/repos/{REPO}/{path}",
        data=json.dumps(data).encode() if data is not None else None,
        headers={"Authorization": f"Bearer {os.environ['GH_TOKEN']}",
                 "Accept": "application/vnd.github+json",
                 "Content-Type": "application/json",
                 "X-GitHub-Api-Version": "2022-11-28"},
        method="PUT" if data is not None else "GET",
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate():
    request = json.loads(Path(".github/release-request.json").read_text())
    version, source = request["version"], request["source_sha"]
    require(re.fullmatch(r"\d+\.\d+\.\d+", version), "Invalid version")
    require(re.fullmatch(r"[0-9a-f]{40}", source), "Invalid source SHA")
    require(type(request["build_version"]) is int and request["build_version"] > 0,
            "Invalid build version")
    require(type(request["run_id"]) is int and type(request["artifact_id"]) is int,
            "Invalid build/artifact IDs")
    run = api(f"actions/runs/{request['run_id']}")
    require(run["status"] == "completed" and run["conclusion"] == "success",
            "Selected build has not passed")
    require(run["head_sha"] == source and run["head_repository"]["full_name"] == REPO,
            "Build source mismatch")
    require(run["path"] == ".github/workflows/windows-trial-build.yml",
            "Unexpected build workflow")
    artifacts = api(f"actions/runs/{request['run_id']}/artifacts")["artifacts"]
    matching = [a for a in artifacts if a["id"] == request["artifact_id"]]
    require(len(matching) == 1 and not matching[0]["expired"], "Artifact unavailable")
    require(matching[0]["name"] == f"ProviTracker-Windows-TRIAL-{run['run_number']}",
            "Unexpected artifact")
    subprocess.run(["git", "merge-base", "--is-ancestor", source, "HEAD"], check=True)
    allowed = {".github/release-request.json", ".github/workflows/publish-release.yml",
               "scripts/publish_tested_release.py"}
    changed = set(git("diff", "--name-only", source, "HEAD").splitlines())
    require(changed <= allowed, f"Untested changes: {changed - allowed}")
    main = Path("main_v32.cpp").read_text()
    require(f'APP_VERSION = "{version}"' in main or
            re.search(r'APP_VERSION\s*=\s*"' + re.escape(version) + '"', main),
            "Application version mismatch")
    require(re.search(r"APP_BUILD_VERSION\s*=\s*" + str(request["build_version"]) + r"\b", main),
            "Build version mismatch")
    require(version in Path("installer/ProviTracker.iss").read_text() and
            version in Path("CMakeLists.txt").read_text(), "Package version mismatch")
    return request


def publish(request):
    version = request["version"]
    folder = Path("release-artifact")
    installer = folder / f"ProviBeregnerSetup-{version}.exe"
    archive = folder / "ProviTracker-TRIAL-update.zip"
    readme = (folder / "TRIAL-README.txt").read_text(encoding="utf-8-sig")
    require(f"Source commit: {request['source_sha']}" in readme, "Package source mismatch")
    require(installer.is_file() and installer.stat().st_size > 1_000_000,
            "Missing or empty installer")
    installer_hash = digest(installer)
    with zipfile.ZipFile(archive) as package:
        require(package.namelist() == [installer.name], "Unexpected update ZIP contents")
        with package.open(installer.name) as stream:
            require(hashlib.file_digest(stream, "sha256").hexdigest() == installer_hash,
                    "ZIP installer differs from standalone installer")
    update = folder / f"ProviTrackerUpdate-{version}.zip"
    archive.rename(update)
    assets = [installer, update]
    release = api("releases/tags/autoupdate")
    require(not release["draft"] and not release["prerelease"], "Autoupdate release is not public")
    existing = {a["name"] for a in release["assets"]}
    current_feed = api("contents/appcast.xml?ref=main")
    root = ET.fromstring(base64.b64decode(current_feed["content"]))
    item = root.find("channel/item")
    enclosure = item.find("enclosure")
    require(int(enclosure.get(f"{{{SPARKLE}}}version")) < request["build_version"],
            "This version or a newer version is already active")
    require(api("git/ref/heads/main")["object"]["sha"] == os.environ["GITHUB_SHA"],
            "Main changed during publication; start a new reviewed release")
    for asset in assets:
        if asset.name not in existing:
            subprocess.run(["gh", "release", "upload", "autoupdate", str(asset),
                            "--repo", REPO], check=True)
        # Verify the downloadable bytes, including assets retained from a retry.
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run(["gh", "release", "download", "autoupdate", "--repo", REPO,
                            "--pattern", asset.name, "--dir", directory], check=True)
            require(digest(Path(directory) / asset.name) == digest(asset),
                    f"Published asset mismatch: {asset.name}")
    with tempfile.TemporaryDirectory() as directory:
        notes = Path(directory) / "notes.md"
        notes.write_text(f"Provi Tracker {version}\n\n" + request["release_notes"] + "\n")
        subprocess.run(["gh", "release", "edit", "autoupdate", "--repo", REPO,
                        "--title", f"v{version}", "--notes-file", str(notes)], check=True)
    base_url = f"https://github.com/{REPO}/releases/download/autoupdate/"
    item.find("title").text = f"Version {version}"
    item.find("pubDate").text = email.utils.format_datetime(datetime.datetime.now(datetime.timezone.utc))
    enclosure.set("url", base_url + installer.name)
    enclosure.set("length", str(installer.stat().st_size))
    enclosure.set(f"{{{SPARKLE}}}version", str(request["build_version"]))
    enclosure.set(f"{{{SPARKLE}}}shortVersionString", version)
    enclosure.set(f"{{{PROVI}}}zipUrl", base_url + update.name)
    enclosure.set(f"{{{PROVI}}}zipLength", str(update.stat().st_size))
    enclosure.set(f"{{{PROVI}}}zipSha256", digest(update))
    ET.register_namespace("sparkle", SPARKLE)
    ET.register_namespace("provi", PROVI)
    ET.indent(root)
    content = ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"
    require(api("git/ref/heads/main")["object"]["sha"] == os.environ["GITHUB_SHA"],
            "Main changed before activation; update feed remains unchanged")
    result = api("contents/appcast.xml", {
        "message": f"Activate tested ProviTracker {version} release",
        "branch": "main", "sha": current_feed["sha"],
        "content": base64.b64encode(content).decode(),
    })
    live = api("contents/appcast.xml?ref=main")
    require(base64.b64decode(live["content"]) == content, "Feed verification failed")
    print(f"Published {version}; feed commit {result['commit']['sha']}")
    for asset in assets:
        print(f"{asset.name}: {asset.stat().st_size} bytes SHA256 {digest(asset)}")


if __name__ == "__main__":
    request = validate()
    if sys.argv[1] == "validate":
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write(f"run_id={request['run_id']}\nartifact_id={request['artifact_id']}\n")
    elif sys.argv[1] == "publish":
        publish(request)
    else:
        raise ValueError("Expected validate or publish")
