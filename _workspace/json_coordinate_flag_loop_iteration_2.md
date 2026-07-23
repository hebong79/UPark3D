# JSON 좌표 플래그 Loop Iteration 2 — Live Coding 링크 환경 진단

## 실패 원인 분리

수동 Live Coding의 UBT 결과는 다음 순서였다.

1. UHT: `Generated code is up to date`.
2. C++ 컴파일: `Compile SharedPCH...`, `Compile Module.Park3D.cpp` 두 작업이 완료되고 UBT `Result: Succeeded`.
3. Live Coding 패치 링크: VS2022 `14.44.35207`의 `link.exe`가 이전 VS18 `14.50.35717` 경로의 `MSVCRT.lib`를 요구하여 `LNK1181`.

따라서 이번 실패는 현재 C++ 소스의 컴파일/반사 실패가 아니라 **기존 Live Coding 링크 응답 파일의 stale MSVC 경로**다. 코드 수정은 하지 않았다.

## 재현 근거

- 실제 설치: VS2022 MSVC `14.44.35207`의 `MSVCRT.lib` 존재.
- VS18에는 MSVC `14.51.36231`만 있고, 링크가 요구한 `14.50.35717` 경로/파일은 없음.
- `Intermediate/Build/.../UnrealEditor-Park3D.dll.rsp.old`에 `/LIBPATH:"C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/lib/x64"`가 남아 있음.
- `Park3D.log`는 VS2022 `14.44.35207` link.exe 실행 직후 위 VS18 lib 경로로 LNK1181을 기록.

## 안전한 대체 검증 경로 및 결과

1. 에디터/PIE 종료 후 `Build.bat Park3DEditor Win64 Development D:\Work\UnrealWork\Parking\Park3D\Park3D.uproject -WaitMutex -NoHotReload`을 실행했다.
2. VS2022 `14.44.35207`로 SharedPCH/Module.Park3D 컴파일, `UnrealEditor-Park3D.lib` 및 `.dll` 링크를 포함한 6 action이 모두 성공했다. UBT `Result: Succeeded` (49.69초).
3. 새 에디터를 재시작해 full build DLL을 로드했다.
4. Automation 10건(아래 QA 표)과 PIE 시작→실행 상태→중지를 실행했다.

중간 `.rsp.old`와 사용자 설치/구성을 수동 삭제·수정하지 않았다. full build 링크 성공으로 이번 Goal에 대한 stale Live Coding 경로는 우회·해소됐다. 향후 Live Coding만 재발하면 환경 캐시 복구를 별도 작업으로 처리한다.
