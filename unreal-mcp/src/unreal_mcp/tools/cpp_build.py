"""C++ / 빌드 도구: 클래스 스캐폴딩(파일), UBT 컴파일, 라이브 코딩, 패키징, 잡 관리.

C++ 소스 '편집'은 클라이언트(Claude Code)의 파일 도구가 담당한다 — 여기서는
스캐폴딩과 빌드 검증만 제공한다 (설계서 4.5).
"""

from pathlib import Path

from .. import config, jobs, runner
from ..server import mcp

# 부모 클래스 → (include 경로, 접두사) 매핑
_PARENTS: dict[str, tuple[str, str]] = {
    "Actor": ("GameFramework/Actor.h", "A"),
    "Pawn": ("GameFramework/Pawn.h", "A"),
    "Character": ("GameFramework/Character.h", "A"),
    "GameModeBase": ("GameFramework/GameModeBase.h", "A"),
    "PlayerController": ("GameFramework/PlayerController.h", "A"),
    "ActorComponent": ("Components/ActorComponent.h", "U"),
    "SceneComponent": ("Components/SceneComponent.h", "U"),
    "Object": ("UObject/Object.h", "U"),
    "UserWidget": ("Blueprint/UserWidget.h", "U"),
}

_HEADER_TEMPLATE = """\
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "{parent_include}"
#include "{class_name}.generated.h"

UCLASS()
class {api_macro} {prefix}{class_name} : public {prefix_parent}{parent}
{{
\tGENERATED_BODY()

public:

\t{prefix}{class_name}();
}};
"""

_CPP_TEMPLATE = """\
// Copyright Epic Games, Inc. All Rights Reserved.

#include "{include_path}"

{prefix}{class_name}::{prefix}{class_name}()
{{
}}
"""

_LIVE_CODING_BODY = """\
import unreal
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "LiveCoding.Compile")
return {"triggered": "LiveCoding.Compile",
        "note": "결과는 log_read 또는 에디터 Live Coding 창에서 확인"}
"""


@mcp.tool()
def cpp_new_class(class_name: str, parent_class: str = "Actor", sub_dir: str = "") -> dict:
    """C++ 클래스 헤더/소스를 Source/<모듈>/ 아래에 생성한다 (에디터 불필요).

    parent_class 지원: Actor, Pawn, Character, GameModeBase, PlayerController,
    ActorComponent, SceneComponent, Object, UserWidget.
    sub_dir: 모듈 내 하위 폴더 (예: "Gameplay"). 생성 후 build_compile로 검증할 것.
    """
    parent = parent_class.removeprefix("A").removeprefix("U") \
        if parent_class[:1] in ("A", "U") and parent_class[1:2].isupper() else parent_class
    if parent not in _PARENTS:
        return {"ok": False, "error": f"unsupported parent_class: {parent_class}",
                "supported": sorted(_PARENTS)}
    if not class_name.isidentifier():
        return {"ok": False, "error": f"invalid class_name: {class_name}"}

    module = config.project_name()
    module_dir = (config.project_dir() / "Source" / module).resolve()
    target_dir = (module_dir / sub_dir).resolve() if sub_dir else module_dir
    if not target_dir.is_relative_to(module_dir):
        return {"ok": False, "error": "sub_dir must stay inside the module directory"}
    target_dir.mkdir(parents=True, exist_ok=True)

    header_path = target_dir / f"{class_name}.h"
    cpp_path = target_dir / f"{class_name}.cpp"
    if header_path.exists() or cpp_path.exists():
        return {"ok": False, "error": f"file_exists: {header_path.name} / {cpp_path.name}"}

    parent_include, prefix = _PARENTS[parent]
    include_path = (Path(sub_dir) / f"{class_name}.h").as_posix() if sub_dir else f"{class_name}.h"
    header_path.write_text(_HEADER_TEMPLATE.format(
        parent_include=parent_include, class_name=class_name,
        api_macro=f"{module.upper()}_API", prefix=prefix,
        prefix_parent=prefix, parent=parent), encoding="utf-8")
    cpp_path.write_text(_CPP_TEMPLATE.format(
        include_path=include_path, prefix=prefix, class_name=class_name),
        encoding="utf-8")
    return {"ok": True, "data": {
        "header": str(header_path), "cpp": str(cpp_path),
        "class": f"{prefix}{class_name}",
        "next": "build_compile 또는 build_hot_reload로 컴파일하세요"}}


@mcp.tool()
def build_compile(target: str = "", build_config: str = "Development") -> dict:
    """UnrealBuildTool로 컴파일을 시작하고 즉시 job_id를 반환한다. job_status로 폴링할 것.

    target 기본값: "<프로젝트>Editor" (에디터 타깃).
    """
    try:
        engine = config.engine_root()
    except config.ConfigError as e:
        return {"ok": False, "error": str(e)}
    build_bat = engine / "Engine" / "Build" / "BatchFiles" / "Build.bat"
    if not build_bat.is_file():
        return {"ok": False, "error": f"build_bat_not_found: {build_bat}"}
    target = target or f"{config.project_name()}Editor"
    cmd = ["cmd.exe", "/c", str(build_bat), target, "Win64", build_config,
           f"-Project={config.project_file()}", "-WaitMutex"]
    return jobs.start(f"compile {target} {build_config}", cmd)


@mcp.tool()
def build_hot_reload() -> dict:
    """실행 중인 에디터에서 라이브 코딩 컴파일(Ctrl+Alt+F11 상당)을 트리거한다."""
    return runner.run(_LIVE_CODING_BODY)


@mcp.tool()
def build_package(platform: str = "Win64", build_config: str = "Development") -> dict:
    """UAT BuildCookRun으로 쿡+패키징을 시작하고 즉시 job_id를 반환한다. 결과물은 Saved/Packages."""
    try:
        engine = config.engine_root()
    except config.ConfigError as e:
        return {"ok": False, "error": str(e)}
    uat = engine / "Engine" / "Build" / "BatchFiles" / "RunUAT.bat"
    if not uat.is_file():
        return {"ok": False, "error": f"uat_not_found: {uat}"}
    archive_dir = config.project_dir() / "Saved" / "Packages"
    cmd = ["cmd.exe", "/c", str(uat), "BuildCookRun",
           f"-project={config.project_file()}",
           f"-platform={platform}", f"-clientconfig={build_config}",
           "-build", "-cook", "-stage", "-pak",
           "-archive", f"-archivedirectory={archive_dir}",
           "-unattended", "-noP4"]
    return jobs.start(f"package {platform} {build_config}", cmd)


@mcp.tool()
def job_status(job_id: str, log_lines: int = 30) -> dict:
    """비동기 잡(build_compile/build_package)의 상태와 로그 tail을 조회한다."""
    return jobs.status(job_id, log_lines)


@mcp.tool()
def job_cancel(job_id: str) -> dict:
    """실행 중인 잡을 프로세스 트리째 중단한다."""
    return jobs.cancel(job_id)
