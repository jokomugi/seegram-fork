import json
import subprocess
import sys

result = subprocess.check_output([sys.argv[1], sys.argv[2], "diagnostics"], text=True)
report = json.loads(result)
assert report["schema"] == 1
assert report["application_version"] == '7.3-personal.1"test'
assert report["host_interface"] == 1
modules = {item["id"]: item for item in report["modules"]}
assert modules["healthy"]["state"] == "running"
assert modules["untrusted-output"]["error"] == "invalid_response"
assert 1 <= len(report["events"]) <= 64
assert "private-message" not in result
assert "api-key" not in result
assert "TOKEN=" not in result
assert sys.argv[2] not in result
print("PASS: valid structured report excludes worker output and local paths")
