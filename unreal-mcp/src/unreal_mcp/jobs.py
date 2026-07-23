"""UBT/UAT 장시간 작업용 비동기 잡 매니저. 로그는 Saved/MCPJobs/<job_id>.log."""

import logging
import subprocess
import threading
import time
import uuid
from dataclasses import dataclass
from pathlib import Path

from . import config

_log = logging.getLogger("unreal_mcp.jobs")


@dataclass
class _Job:
    id: str
    name: str
    cmd: list[str]
    proc: subprocess.Popen
    log_file: object  # 열린 파일 핸들 (종료 시 close)
    log_path: Path
    started: float


_jobs: dict[str, _Job] = {}
_lock = threading.Lock()


def start(name: str, cmd: list[str]) -> dict:
    job_id = "j_" + uuid.uuid4().hex[:8]
    log_path = config.jobs_dir() / f"{job_id}.log"
    log_file = open(log_path, "wb")
    log_file.write(("$ " + " ".join(cmd) + "\n\n").encode("utf-8"))
    log_file.flush()
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            cwd=str(config.project_dir()),
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        )
    except OSError as e:
        log_file.close()
        return {"ok": False, "error": f"spawn_failed: {e}"}
    with _lock:
        _jobs[job_id] = _Job(job_id, name, cmd, proc, log_file, log_path, time.time())
    _log.info("잡 시작 [%s] %s (pid %s, log: %s)", job_id, name, proc.pid, log_path.name)
    return {"ok": True, "data": {"job_id": job_id, "name": name, "state": "running",
                                 "log_file": str(log_path)}}


def _tail(path: Path, lines: int) -> list[str]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    return text.splitlines()[-lines:]


def status(job_id: str, log_lines: int = 30) -> dict:
    with _lock:
        job = _jobs.get(job_id)
    if job is None:
        return {"ok": False, "error": "job_not_found"}
    rc = job.proc.poll()
    if rc is None:
        state = "running"
    else:
        state = "succeeded" if rc == 0 else "failed"
        _log.info("잡 종료 [%s] %s → %s (exit %s)", job.id, job.name, state, rc)
        try:
            job.log_file.close()
        except OSError:
            pass
    return {"ok": True, "data": {
        "job_id": job.id,
        "name": job.name,
        "state": state,
        "exit_code": rc,
        "duration_s": round(time.time() - job.started, 1),
        "log_tail": _tail(job.log_path, log_lines),
        "log_file": str(job.log_path),
    }}


def cancel(job_id: str) -> dict:
    with _lock:
        job = _jobs.get(job_id)
    if job is None:
        return {"ok": False, "error": "job_not_found"}
    if job.proc.poll() is not None:
        return {"ok": False, "error": "already_finished", "exit_code": job.proc.returncode}
    # UBT/UAT는 자식 프로세스를 거느리므로 트리 전체를 종료
    subprocess.run(
        ["taskkill", "/PID", str(job.proc.pid), "/T", "/F"],
        capture_output=True,
    )
    return {"ok": True, "data": {"job_id": job_id, "state": "canceled"}}
