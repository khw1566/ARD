import urllib.request
import re
import sys

sys.stdout.reconfigure(encoding='utf-8')

# Search naver blog or news for patent numbers of automated recycling machines
url = "https://lite.duckduckgo.com/lite/"
data = urllib.parse.urlencode({"q": "특허 '재활용품 자동 회수' 등록번호 10-"}).encode("utf-8")
req = urllib.request.Request(url, data=data, headers={"User-Agent": "Mozilla/5.0"})
try:
    html = urllib.request.urlopen(req, timeout=10).read().decode("utf-8", errors="ignore")
    # find patent numbers like 10-XXXXXXX
    matches = re.findall(r'10-\d{7}|10-\d{4}-\d{7}', html)
    print("Matches:", set(matches))
    for line in html.split('\n'):
        if any(m in line for m in matches):
            clean = re.sub(r'<[^>]+>', ' ', line).strip()
            print("Line:", clean[:150])
except Exception as e:
    print("Err:", e)
