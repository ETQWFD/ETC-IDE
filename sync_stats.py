#!/usr/bin/env python3
import json, time, subprocess, os, sys

TOKEN = os.environ.get("STATS_TOKEN", "")
if not TOKEN:
    sys.exit("STATS_TOKEN not set")

REGION_MAP = {
    "Beijing": "北京市", "Tianjin": "天津市", "Shanghai": "上海市", "Chongqing": "重庆市",
    "Hebei": "河北省", "Shanxi": "山西省", "Liaoning": "辽宁省", "Jilin": "吉林省",
    "Heilongjiang": "黑龙江省", "Jiangsu": "江苏省", "Zhejiang": "浙江省", "Anhui": "安徽省",
    "Fujian": "福建省", "Jiangxi": "江西省", "Shandong": "山东省", "Henan": "河南省",
    "Hubei": "湖北省", "Hunan": "湖南省", "Guangdong": "广东省", "Hainan": "海南省",
    "Sichuan": "四川省", "Guizhou": "贵州省", "Yunnan": "云南省", "Shaanxi": "陕西省",
    "Gansu": "甘肃省", "Qinghai": "青海省", "Inner Mongolia": "内蒙古自治区",
    "Guangxi": "广西壮族自治区", "Tibet": "西藏自治区", "Ningxia": "宁夏回族自治区",
    "Xinjiang": "新疆维吾尔自治区", "Taiwan": "台湾省", "Hong Kong": "香港特别行政区",
    "Macau": "澳门特别行政区",
}

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
    region = r.get("region") or ""
    province = c.get("province") or REGION_MAP.get(region, region)
    city = c.get("city") or (r.get("city") or "")
    records.append({
        "ip": r.get("ip") or c.get("ip") or "",
        "province": province,
        "city": city,
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
