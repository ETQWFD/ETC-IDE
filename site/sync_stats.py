#!/usr/bin/env python3
import json, time, subprocess, os, sys

TOKEN = os.environ.get("STATS_TOKEN", "")
if not TOKEN:
    sys.exit("STATS_TOKEN not set")

URL = f"https://webhook.site/token/{TOKEN}/requests?sorting=newest&per_page=100"
raw = subprocess.run(
    ["curl", "-s", "-m", "30", "-H", "Accept: application/json", URL],
    capture_output=True, text=True,
)
if raw.returncode != 0:
    sys.exit("curl failed: " + raw.stderr)
data = json.loads(raw.stdout)

records = []
for r in data.get("data", []):
    if r.get("method") != "POST":
        continue
    try:
        c = json.loads(r.get("content") or "{}")
    except Exception:
        continue
    if not c or c.get("action") != "download":
        continue
    ts = c.get("t")
    if isinstance(ts, (int, float)):
        t = int(ts)
    else:
        try:
            t = int(time.mktime(time.strptime(r["created_at"][:19], "%Y-%m-%dT%H:%M:%S")) * 1000)
        except Exception:
            t = int(time.time() * 1000)
    records.append({
        "ip": r.get("ip") or c.get("ip") or "",
        "province": c.get("province") or "",
        "city": c.get("city") or "",
        "file": c.get("file") or "",
        "ua": c.get("ua") or "",
        "t": t,
    })

out = {
    "updated": time.strftime("%Y-%m-%d %H:%M:%S"),
    "downloads": len(records),
    "records": records,
}
with open("stats.json", "w", encoding="utf-8") as f:
    json.dump(out, f, ensure_ascii=False, indent=1)
print(f"ok records={len(records)} updated={out['updated']}")
