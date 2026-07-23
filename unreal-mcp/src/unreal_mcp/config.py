"""프로젝트/엔진 경로 해석. 환경변수 우선, 없으면 자동 탐색."""

import json
import os
from functools import lru_cache
from pathlib import Path


class ConfigError(RuntimeError):
    pass


@lru_cache(maxsize=1)
def project_file() -> Path:
    env = os.environ.get("UE_PROJECT")
    if env:
        p = Path(env)
        if p.is_file():
            return p
        raise ConfigError(f"UE_PROJECT가 가리키는 파일이 없습니다: {env}")
    here = Path(__file__).resolve()
    for parent in here.parents:
        hits = sorted(parent.glob("*.uproject")) or sorted(parent.glob("*/*.uproject"))
        if hits:
            return hits[0]
    raise ConfigError(".uproject를 찾지 못했습니다. UE_PROJECT 환경변수를 설정하세요.")


def project_dir() -> Path:
    return project_file().parent


def project_name() -> str:
    return project_file().stem


@lru_cache(maxsize=1)
def uproject() -> dict:
    return json.loads(project_file().read_text(encoding="utf-8-sig"))


@lru_cache(maxsize=1)
def engine_root() -> Path:
    env = os.environ.get("UE_ENGINE_ROOT")
    if env:
        p = Path(env)
        if p.is_dir():
            return p
        raise ConfigError(f"UE_ENGINE_ROOT 디렉터리가 없습니다: {env}")
    ver = str(uproject().get("EngineAssociation", ""))
    try:
        import winreg

        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE, rf"SOFTWARE\EpicGames\Unreal Engine\{ver}"
        ) as key:
            installed, _ = winreg.QueryValueEx(key, "InstalledDirectory")
        p = Path(installed)
        if p.is_dir():
            return p
    except OSError:
        pass
    p = Path(rf"C:\Program Files\Epic Games\UE_{ver}")
    if p.is_dir():
        return p
    raise ConfigError(
        f"UE {ver} 엔진을 찾지 못했습니다. UE_ENGINE_ROOT 환경변수를 설정하세요."
    )


def editor_log_file() -> Path:
    return project_dir() / "Saved" / "Logs" / f"{project_name()}.log"


def jobs_dir() -> Path:
    d = project_dir() / "Saved" / "MCPJobs"
    d.mkdir(parents=True, exist_ok=True)
    return d


def screenshots_dir() -> Path:
    d = project_dir() / "Saved" / "Screenshots" / "MCP"
    d.mkdir(parents=True, exist_ok=True)
    return d
