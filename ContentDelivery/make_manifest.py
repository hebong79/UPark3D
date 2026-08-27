# -*- coding: utf-8 -*-
"""
배포 스냅샷 목록(MANIFEST.txt)을 다시 만든다.

이 폴더의 파일이 `Park3D/Content` 의 어느 시점 사본인지를 해시로 남겨,
**낡았는지 판정할 수 있게** 하는 것이 목적이다. 바이너리라 diff 가 안 되므로
해시 말고는 stale 여부를 알 방법이 없다.

    python ContentDelivery/make_manifest.py
"""

import datetime
import hashlib
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC_ROOT = os.path.join(ROOT, "Park3D")


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
    lines = []
    base = os.path.join(HERE, "Content")
    for dirpath, _, files in os.walk(base):
        for name in sorted(files):
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, HERE).replace(os.sep, "/")
            src = os.path.join(SRC_ROOT, rel)
            stamp = "원본 없음"
            same = "?"
            if os.path.exists(src):
                stamp = datetime.datetime.fromtimestamp(
                    os.path.getmtime(src)).strftime("%Y-%m-%d %H:%M:%S")
                same = "일치" if sha(src) == sha(path) else "**다름**"
            lines.append("%s  %-58s %8d B  원본수정 %s  %s"
                         % (sha(path)[:16], rel, os.path.getsize(path), stamp, same))

    text = ("# 배포 스냅샷 목록 — sha256 앞 16자 / 경로 / 크기 / 원본(Park3D/...) 수정시각 / 원본과 일치 여부\n"
            "# 이 파일이 낡았는지 보려면 `python ContentDelivery/make_manifest.py` 를 다시 돌려\n"
            "# 마지막 칸이 전부 '일치' 인지 확인한다.\n"
            + "\n".join(lines) + "\n")
    out = os.path.join(HERE, "MANIFEST.txt")
    open(out, "w", encoding="utf-8").write(text)
    print(text)


if __name__ == "__main__":
    main()
