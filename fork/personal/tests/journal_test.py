import json
import subprocess
import sys

raw = subprocess.check_output([sys.argv[1], sys.argv[2], "journal"], text=True)
report = json.loads(raw)
assert report["modules"][0]["state"] == "running"
assert any(event["error"] == "invalid_response" for event in report["events"])
assert all(event["time_ms"] > 0 for event in report["events"])
assert report["journal_available"] is True
assert "private-message" not in raw
assert "api-key" not in raw
print("PASS: structured failure history survives a successful restart")
