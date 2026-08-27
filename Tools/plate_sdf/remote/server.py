# -*- coding: utf-8 -*-
"""
번호판 글리프 SDF 베이크 서비스 (FastAPI, 포트 8090).

정책: 이 프로젝트는 연구단계라 인증·토큰·권한 검사를 넣지 않는다(`park3d-no-security`).
방화벽·OS 보안 설정도 손대지 않는다.

API
  GET  /health                    서버·장치 상태
  POST /bake                      multipart: font(파일) + 옵션 → {job_id}
  GET  /bake/{job_id}             상태·진행률
  GET  /bake/{job_id}/result      zip (atlas.png + metrics.json)
  DELETE /bake/{job_id}           작업 폴더 삭제

폰트는 **요청에 실어 보낸다**. 서버에 상주시키지 않는 이유는 두 가지 — 라이선스(서버에
남은 폰트가 누구 것인지 추적이 안 된다)와 재현성(클라이언트가 가진 그 파일로 구웠음이
`fontSha256` 으로 증명된다).
"""

import hashlib
import json
import os
import shutil
import threading
import time
import traceback
import uuid
import zipfile

from fastapi import FastAPI, File, Form, HTTPException, UploadFile
from fastapi.responses import FileResponse, JSONResponse

import sdf_core

ROOT = os.path.dirname(os.path.abspath(__file__))
JOBS_DIR = os.path.join(ROOT, "jobs")
os.makedirs(JOBS_DIR, exist_ok=True)

VERSION = "1.0.0"

app = FastAPI(title="Park3D Plate SDF Bake", version=VERSION)

# 작업 상태는 메모리에 둔다(프로세스가 죽으면 진행 중 작업은 사라진다 — 한계로 문서화).
_jobs = {}
_lock = threading.Lock()
# 3090 이 2장이지만 워커는 1개다. 동시 실행은 VRAM 보다 Pillow 래스터(단일 스레드)가 먼저
# 병목이라 얻는 게 적고, 순서가 섞이면 소요시간 측정이 흐려진다.
_gate = threading.Semaphore(1)


def _device_info():
    try:
        device, torch, note = sdf_core.pick_device("auto")
    except Exception as e:                       # noqa: BLE001
        return {"device": "cpu", "deviceNote": str(e), "gpus": []}
    gpus = []
    if device == "cuda":
        gpus = [torch.cuda.get_device_name(i)
                for i in range(torch.cuda.device_count())]
    return {"device": device, "deviceNote": note, "gpus": gpus}


@app.get("/health")
def health():
    import numpy
    import PIL
    from PIL import features
    info = _device_info()
    try:
        import torch
        tv = torch.__version__
    except Exception:                            # noqa: BLE001
        tv = None
    with _lock:
        njobs = len(_jobs)
    return {
        "status": "ok",
        "service": "park3d-plate-bake",
        "version": VERSION,
        "device": info["device"],
        "deviceNote": info["deviceNote"],
        "gpus": info["gpus"],
        "torch": tv,
        "numpy": numpy.__version__,
        "pillow": PIL.__version__,
        "freetype": features.version("freetype2"),
        "raqm": features.version("raqm"),
        "layoutEngine": "BASIC(고정)",
        "jobs": njobs,
    }


def _run(job_id, font_path, opts):
    job = _jobs[job_id]
    with _gate:
        job["state"] = "running"
        job["startedAt"] = time.time()
        d = os.path.join(JOBS_DIR, job_id)
        try:
            def prog(done, total):
                job["done"] = done
                job["total"] = total
                job["progress"] = round(done / total, 4) if total else 0.0

            res = sdf_core.bake(
                font_path,
                os.path.join(d, "atlas.png"),
                os.path.join(d, "metrics.json"),
                progress=prog,
                **opts,
            )
            with zipfile.ZipFile(os.path.join(d, "result.zip"), "w",
                                 zipfile.ZIP_DEFLATED) as z:
                z.write(os.path.join(d, "atlas.png"), "atlas.png")
                z.write(os.path.join(d, "metrics.json"), "metrics.json")
            job.update(res)
            job["state"] = "done"
            job["progress"] = 1.0
        except Exception as e:                   # noqa: BLE001
            job["state"] = "error"
            job["error"] = f"{type(e).__name__}: {e}"
            job["traceback"] = traceback.format_exc()
        finally:
            job["finishedAt"] = time.time()
            job["elapsed"] = round(job["finishedAt"] - job["startedAt"], 3)
            # 업로드된 폰트는 굽고 나면 지운다(서버에 폰트를 남기지 않는다).
            try:
                os.remove(font_path)
            except OSError:
                pass


@app.post("/bake")
async def bake(
    font: UploadFile = File(..., description="TTF/OTF 파일"),
    glyphs: str = Form(sdf_core.GLYPHS),
    cell: int = Form(sdf_core.CELL),
    spread: float = Form(sdf_core.SPREAD),
    ss: int = Form(sdf_core.SS),
    cols: int = Form(sdf_core.COLS),
    baseline: int = Form(sdf_core.BASELINE),
    font_size: int = Form(sdf_core.FONT_SIZE),
    label: str = Form(""),
    device: str = Form("auto"),
    batch: int = Form(8),
):
    if device not in ("auto", "cuda", "cpu"):
        raise HTTPException(400, "device 는 auto|cuda|cpu")
    if not glyphs:
        raise HTTPException(400, "glyphs 가 비었다")
    if cell % ss:
        raise HTTPException(400, f"cell({cell}) 은 ss({ss}) 로 나누어떨어져야 한다")

    data = await font.read()
    if not data:
        raise HTTPException(400, "폰트 파일이 비었다")

    job_id = uuid.uuid4().hex[:16]
    d = os.path.join(JOBS_DIR, job_id)
    os.makedirs(d, exist_ok=True)
    font_path = os.path.join(d, os.path.basename(font.filename or "font.ttf"))
    with open(font_path, "wb") as f:
        f.write(data)

    opts = dict(glyphs=glyphs, cell=cell, spread=spread, ss=ss, cols=cols,
                baseline=baseline, font_size=font_size,
                label=label or os.path.basename(font_path),
                device_want=device, batch=batch)

    _jobs[job_id] = {
        "jobId": job_id,
        "state": "queued",
        "progress": 0.0,
        "done": 0,
        "total": len(glyphs),
        "fontFile": os.path.basename(font_path),
        "fontSha256": hashlib.sha256(data).hexdigest(),
        "fontBytes": len(data),
        "options": {k: v for k, v in opts.items()},
        "queuedAt": time.time(),
    }
    threading.Thread(target=_run, args=(job_id, font_path, opts),
                     daemon=True).start()
    return {"jobId": job_id, "state": "queued", "total": len(glyphs)}


@app.get("/bake/{job_id}")
def status(job_id: str):
    j = _jobs.get(job_id)
    if not j:
        raise HTTPException(404, "그런 job_id 가 없다")
    out = dict(j)
    out.pop("traceback", None)
    if j["state"] == "running":
        out["elapsed"] = round(time.time() - j["startedAt"], 3)
    return JSONResponse(out)


@app.get("/bake/{job_id}/result")
def result(job_id: str):
    j = _jobs.get(job_id)
    if not j:
        raise HTTPException(404, "그런 job_id 가 없다")
    if j["state"] != "done":
        raise HTTPException(409, f"아직 준비되지 않았다 (state={j['state']})")
    p = os.path.join(JOBS_DIR, job_id, "result.zip")
    if not os.path.exists(p):
        raise HTTPException(500, "result.zip 이 사라졌다")
    return FileResponse(p, media_type="application/zip",
                        filename=f"plate_sdf_{job_id}.zip")


@app.delete("/bake/{job_id}")
def drop(job_id: str):
    _jobs.pop(job_id, None)
    shutil.rmtree(os.path.join(JOBS_DIR, job_id), ignore_errors=True)
    return {"ok": True}
