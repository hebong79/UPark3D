# unreal-mcp

언리얼 에디터(Parking, UE 5.7)를 MCP 클라이언트(Claude Code 등)에서 제어하는 서버.
설계: [../Docs/UNREAL_MCP_DESIGN.md](../Docs/UNREAL_MCP_DESIGN.md)

## 설치

```sh
pip install -e d:/Work2/Unreal2/Parking3D/unreal-mcp
```

서버 등록은 워크스페이스 루트의 `.mcp.json`에 이미 되어 있다.

## 언리얼 측 사전 요구사항 (1회)

1. `Parking.uproject`에 `PythonScriptPlugin`, `RemoteControl` 활성화 — **적용 완료**
2. `Config/DefaultEngine.ini`에 Python Remote Execution 설정 — **적용 완료**
   ```ini
   [/Script/PythonScriptPlugin.PythonScriptPluginSettings]
   bRemoteExecution=True
   RemoteExecutionMulticastGroupEndpoint=239.0.0.1:6766
   RemoteExecutionMulticastBindAddress=127.0.0.1
   RemoteExecutionMulticastTtl=0
   ```
3. **에디터를 재시작**하면 플러그인이 로드되고 원격 실행이 켜진다.
   (최초 1회 플러그인 활성화 확인 창이 뜰 수 있음)

## 동작 확인

에디터를 켠 상태에서:

```sh
python -m unreal_mcp   # stdio 서버 기동 (MCP 클라이언트가 실행)
```

Claude Code에서 `/mcp`로 unreal 서버 연결을 확인한 뒤 `editor_status` 도구를 호출하면
`editor_connected: true`가 나와야 한다.

## 도구 요약

| 영역 | 도구 |
|---|---|
| 진단 | `editor_status`, `log_read` |
| 콘솔 | `console_exec`, `cvar_get` |
| 뷰포트 | `viewport_screenshot`, `viewport_get_camera`, `viewport_set_camera`, `viewport_focus_actor` |
| 액터 | `actor_list`, `actor_spawn`, `actor_get_properties`, `actor_set_property`, `actor_set_transform`, `actor_delete`, `actor_select` |
| 애셋 | `asset_search`, `asset_info`, `asset_import`, `asset_save_all` |
| 블루프린트 | `bp_create`, `bp_add_component`, `bp_add_variable`, `bp_get_summary`, `bp_compile`, `bp_set_default` |
| C++/빌드 | `cpp_new_class`, `build_compile`, `build_hot_reload`, `build_package`, `job_status`, `job_cancel` |
| UMG | `umg_create_widget`, `umg_get_tree`, `umg_add_child`, `umg_set_property` |
| 레벨/PIE | `level_open`, `level_save`, `level_current`, `pie_start`, `pie_stop`, `pie_status` |

리소스: `unreal://project/info`, `unreal://project/source-tree`, `unreal://editor/log`, `unreal://level/actors`

## 메모

- 에디터 의존 도구는 에디터 미실행 시 `{ok:false, error:"editor_not_running"}`을 반환한다.
- `build_compile`/`build_package`는 즉시 `job_id`를 반환한다 — `job_status`로 폴링.
- 환경변수: `UE_PROJECT`(uproject 경로), `UE_ENGINE_ROOT`(엔진 설치 경로, 미설정 시 레지스트리/기본 경로 탐색).
