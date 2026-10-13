"""
PlatformIO pre-build script for env:pebl_weather: defines FW_VERSION.

The OTA updater installs a release only if its version is newer than
FW_VERSION (semver precedence), so this must be right:
  * CI release builds:  PEBL_FW_VERSION=1.2.0 (from the weather-v1.2.0 tag)
  * Local builds:       derived from `git describe` against weather-v* tags, as
                        build metadata so it compares EQUAL to the tag it's based
                        on: weather-v1.2.0 + 3 commits -> "1.2.0+dev.3.gabc1234"
                        (a device running it updates to 1.2.1, not to 1.2.0).
                        No tag yet -> "0.0.0+dev.<sha>".
"""
Import("env")
import os, re, subprocess

def describe():
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--match", "weather-v*", "--long", "--dirty"],
            cwd=env["PROJECT_DIR"], capture_output=True, text=True, check=True).stdout.strip()
        m = re.match(r"weather-v(\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?)-(\d+)-g([0-9a-f]+)(-dirty)?$", out)
        if m:
            base, ahead, sha, dirty = m.groups()
            if ahead == "0" and not dirty:
                return base
            return f"{base}+dev.{ahead}.g{sha}" + (".dirty" if dirty else "")
    except (subprocess.CalledProcessError, FileNotFoundError):
        pass
    try:
        sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=env["PROJECT_DIR"],
                             capture_output=True, text=True, check=True).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        sha = "nogit"
    return f"0.0.0+dev.{sha}"

version = os.environ.get("PEBL_FW_VERSION", "").strip() or describe()
if len(version) > 31:
    raise SystemExit(f"FW_VERSION too long for the app descriptor: {version}")
env.Append(CPPDEFINES=[("FW_VERSION", f'\\"{version}\\"')])
env.Replace(PROJECT_VER=version)
print(f"pebl_weather FW_VERSION = {version}")
