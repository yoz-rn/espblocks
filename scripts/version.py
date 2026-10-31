Import("env")
import subprocess

try:
    ver = subprocess.check_output(
        ["git", "describe", "--tags", "--always", "--dirty"]
    ).decode().strip()
except Exception:
    ver = "unknown"

env.Append(CPPDEFINES=[("ESPBLOCKS_VERSION", env.StringifyMacro(ver))])