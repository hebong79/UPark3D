# -*- coding: utf-8 -*-
"""
번호판 글리프 SDF 아틀라스 원격 베이크 클라이언트.

GPU 서버(`~/park3d_plate_bake`, FastAPI :8090)에 폰트를 올려 굽고 결과 zip 을 받아
`Tools/plate_sdf/out/` 에 풀고 `Park3D/Save/Config/plate_glyph_metrics.json` 을 갱신한다.

서버가 죽어 있으면 **로컬 `bake_glyph_sdf.py` 로 자동 폴백**한다. 어느 쪽으로 구웠는지는
항상 stdout 에 한 줄로 찍는다 — 이 프로젝트에서 "값은 들어갔는데 화면이 다른" 사고가
반복됐으므로, 산출물이 어디서 나왔는지를 추측으로 남기지 않는다.

의존성은 표준 라이브러리뿐이다(멀티파트를 직접 만든다).

사용
  python Tools/plate_sdf/remote_bake.py --font <ttf> --label "Pretendard Bold"
  python Tools/plate_sdf/remote_bake.py --font <ttf> --local          # 강제 로컬
  python Tools/plate_sdf/remote_bake.py --font <ttf> --device cpu     # 서버에서 CPU 로
"""

import argparse
import io
import json
import mimetypes
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT_DIR = os.path.join(HERE, "out")
LOCAL_BAKER = os.path.join(HERE, "bake_glyph_sdf.py")
SAVE_CONFIG = os.path.join(REPO, "Park3D", "Save", "Config",
                           "plate_glyph_metrics.json")

DEFAULT_SERVER = "http://192.168.0.210:8090"


# ─── 멀티파트 ───────────────────────────────────────────────────────────────
def _multipart(fields, file_field, file_path):
    boundary = "----park3dbake" + uuid.uuid4().hex
    buf = io.BytesIO()
    for k, v in fields.items():
        buf.write(f"--{boundary}\r\n".encode())
        buf.write(f'Content-Disposition: form-data; name="{k}"\r\n\r\n'.encode())
        buf.write(str(v).encode("utf-8"))
        buf.write(b"\r\n")
    name = os.path.basename(file_path)
    ctype = mimetypes.guess_type(name)[0] or "application/octet-stream"
    buf.write(f"--{boundary}\r\n".encode())
    buf.write((f'Content-Disposition: form-data; name="{file_field}"; '
               f'filename="{name}"\r\n').encode("utf-8"))
    buf.write(f"Content-Type: {ctype}\r\n\r\n".encode())
    with open(file_path, "rb") as f:
        buf.write(f.read())
    buf.write(f"\r\n--{boundary}--\r\n".encode())
    return buf.getvalue(), f"multipart/form-data; boundary={boundary}"


def _get(url, timeout=15, raw=False):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        data = r.read()
    return data if raw else json.loads(data.decode("utf-8"))


# ─── 원격 ───────────────────────────────────────────────────────────────────
def remote_bake(server, font, opts, timeout, poll, verbose=True):
    """성공하면 (zip 바이트, 마지막 상태 dict). 실패하면 예외."""
    health = _get(f"{server}/health", timeout=8)
    if verbose:
        print(f"[원격] {server}  device={health['device']} "
              f"gpus={health.get('gpus')} torch={health.get('torch')} "
              f"pillow={health.get('pillow')}/ft{health.get('freetype')}")

    body, ctype = _multipart(opts, "font", font)
    req = urllib.request.Request(f"{server}/bake", data=body,
                                 headers={"Content-Type": ctype})
    with urllib.request.urlopen(req, timeout=60) as r:
        job = json.loads(r.read().decode("utf-8"))
    job_id = job["jobId"]
    if verbose:
        print(f"[원격] job {job_id}  글리프 {job['total']}자")

    t0 = time.time()
    last = None
    while True:
        last = _get(f"{server}/bake/{job_id}", timeout=15)
        if last["state"] == "done":
            break
        if last["state"] == "error":
            raise RuntimeError(f"서버 베이크 실패: {last.get('error')}")
        if time.time() - t0 > timeout:
            raise TimeoutError(f"{timeout}초 안에 안 끝났다 (state={last['state']})")
        if verbose:
            print(f"\r[원격] {last['state']} "
                  f"{last.get('done', 0)}/{last.get('total', 0)}", end="")
        time.sleep(poll)
    if verbose:
        print(f"\r[원격] done  서버 소요 {last['elapsed']}초  "
              f"device={last.get('device')}{' ' * 20}")
        for w in last.get("warnings") or []:
            print(f"  [경고] {w}")

    return _get(f"{server}/bake/{job_id}/result", timeout=120, raw=True), last


def unpack(zip_bytes, out_png, out_json, label_atlas):
    with zipfile.ZipFile(io.BytesIO(zip_bytes)) as z:
        png = z.read("atlas.png")
        meta = json.loads(z.read("metrics.json").decode("utf-8"))
    os.makedirs(os.path.dirname(out_png) or ".", exist_ok=True)
    with open(out_png, "wb") as f:
        f.write(png)
    # 서버는 자기 파일명("atlas.png")을 적어 보낸다 — 실제 저장 이름으로 고쳐 둔다.
    meta["atlas"] = label_atlas
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=1)
    return meta


# ─── 로컬 폴백 ──────────────────────────────────────────────────────────────
def local_bake(font, out_png, out_json, font_size, label, opts):
    ignored = [k for k in ("glyphs", "cell", "spread", "ss", "cols", "baseline")
               if str(opts.get(k)) != str(DEFAULTS[k])]
    if ignored:
        print(f"  [경고] 로컬 베이커는 {', '.join(ignored)} 인자를 받지 않는다 "
              f"— 기본값으로 굽는다", file=sys.stderr)
    cmd = [sys.executable, LOCAL_BAKER, "--font", font,
           "--out-png", out_png, "--out-json", out_json,
           "--font-size", str(font_size)]
    if label:
        cmd += ["--label", label]
    subprocess.run(cmd, check=True)


DEFAULTS = {"glyphs": "0123456789가나다라마바사거너더러머버서어저고노도로모보소오조구누두루무부수우주",
            "cell": 256, "spread": 8.0, "ss": 4, "cols": 6, "baseline": 200}


def main():
    ap = argparse.ArgumentParser(description="번호판 SDF 원격 베이크")
    ap.add_argument("--font", required=True, help="TTF/OTF 경로")
    ap.add_argument("--server", default=os.environ.get("PARK3D_BAKE_SERVER",
                                                       DEFAULT_SERVER))
    ap.add_argument("--out-png", default=os.path.join(OUT_DIR, "T_PlateGlyphSDF.png"))
    ap.add_argument("--out-json", default=os.path.join(OUT_DIR, "plate_glyph_metrics.json"))
    ap.add_argument("--label", default="")
    ap.add_argument("--font-size", type=int, default=166)
    ap.add_argument("--glyphs", default=DEFAULTS["glyphs"])
    ap.add_argument("--cell", type=int, default=DEFAULTS["cell"])
    ap.add_argument("--spread", type=float, default=DEFAULTS["spread"])
    ap.add_argument("--ss", type=int, default=DEFAULTS["ss"])
    ap.add_argument("--cols", type=int, default=DEFAULTS["cols"])
    ap.add_argument("--baseline", type=int, default=DEFAULTS["baseline"])
    ap.add_argument("--device", default="auto", choices=["auto", "cuda", "cpu"],
                    help="서버에서 쓸 장치")
    ap.add_argument("--batch", type=int, default=8, help="GPU 배치 글리프 수")
    ap.add_argument("--timeout", type=float, default=900)
    ap.add_argument("--poll", type=float, default=0.5)
    ap.add_argument("--local", action="store_true", help="원격을 건너뛰고 로컬로만")
    ap.add_argument("--no-fallback", action="store_true",
                    help="원격 실패 시 로컬로 넘어가지 말고 에러로 끝낸다")
    ap.add_argument("--no-save-config", action="store_true",
                    help="Park3D/Save/Config 갱신을 건너뛴다")
    a = ap.parse_args()

    if not os.path.isfile(a.font):
        sys.exit(f"폰트가 없다: {a.font}")
    label = a.label or os.path.basename(a.font)
    opts = {"glyphs": a.glyphs, "cell": a.cell, "spread": a.spread, "ss": a.ss,
            "cols": a.cols, "baseline": a.baseline, "font_size": a.font_size,
            "label": label, "device": a.device, "batch": a.batch}

    used, t0 = None, time.time()
    if not a.local:
        try:
            zb, st = remote_bake(a.server, a.font, opts, a.timeout, a.poll)
            unpack(zb, a.out_png, a.out_json, os.path.basename(a.out_png))
            used = f"원격 {a.server} (device={st.get('device')}, " \
                   f"서버 소요 {st.get('elapsed')}초)"
        except Exception as e:                   # noqa: BLE001
            msg = f"{type(e).__name__}: {e}"
            if a.no_fallback:
                sys.exit(f"[원격] 실패 — {msg}  (--no-fallback 이라 중단)")
            print(f"[원격] 실패 — {msg}\n[폴백] 로컬 bake_glyph_sdf.py 로 굽는다",
                  file=sys.stderr)

    if used is None:
        local_bake(a.font, a.out_png, a.out_json, a.font_size, label, opts)
        used = "로컬 bake_glyph_sdf.py"

    # 메트릭은 Content/ 가 gitignore 라 git 에 추적되는 Save/Config 에도 둔다.
    if not a.no_save_config:
        os.makedirs(os.path.dirname(SAVE_CONFIG), exist_ok=True)
        shutil.copyfile(a.out_json, SAVE_CONFIG)

    meta = json.load(open(a.out_json, encoding="utf-8"))
    print(f"\n베이크 경로  {used}")
    print(f"총 소요      {time.time() - t0:.2f}초 (전송·압축 해제 포함)")
    print(f"아틀라스     {a.out_png}  "
          f"{meta['cols'] * meta['cell']}x{meta['rows'] * meta['cell']}")
    print(f"메트릭       {a.out_json}  글리프 {len(meta['glyphs'])}자, "
          f"공백 어드밴스 {meta['spaceAdvance']}")
    if not a.no_save_config:
        print(f"Save/Config  {SAVE_CONFIG}")


if __name__ == "__main__":
    main()
