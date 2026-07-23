# 차량 다중 선택 Goal/Loop — Iteration 2

## 직전 실패 원인

- 소스 컴파일 2개 단계는 성공했다.
- Live Coding 패치 링크에서 `LNK1181`이 발생했다.
- 현재 설치된 VS2022 `17.14.36`의 MSVC `14.44.35207`은 존재한다.
- 오류 경로는 제거·재설치 중인 VS2026 `14.50.35717`이어서, Unreal 생성물과 Live Coding 환경에 툴체인 경로가 혼재한 상태로 판정했다.

## 재설계

- Park3D Editor/Game Target에 `VisualStudio2022`와 `14.44.35207`을 명시한다.
- 코드 기능은 변경하지 않고 빌드 환경만 프로젝트 범위로 고정한다.
- 이후 UBT/Live Coding 생성물을 VS2022 기준으로 재생성하고 링크 성공 여부를 확인한다.

## 검증 기준

- UBT 로그에 VS2022 `14.44.35207`이 표시된다.
- `MSVCRT.lib` 탐색 경로가 `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64`로만 구성된다.
- Live Coding patch link가 성공한다.

## 재시도 결과

- `-CompilerVersion=14.44.35207`을 Build.bat에 직접 붙인 첫 시도는 인자 파싱으로 `.44.35207`을 target으로 해석해 중단됐다.
- 인자를 제거한 두 번째 일반 UBT 시도는 `Park3DEditor.Target.cs modified`를 감지했지만, 현재 에디터의 Live Coding 활성 상태 때문에 `Unable to build while Live Coding is active`로 차단됐다.
- 다음 게이트는 에디터에서 `Ctrl+Alt+F11`을 다시 눌러 VS2022 고정 Target 설정을 반영하는 Live Coding 컴파일이다.

## 캐시 우회 설계

- Live Coding 매니페스트와 DLL 응답 파일은 VS2022 `14.44.35207` 경로를 사용한다.
- 그러나 일반 `Module.Park3D.cpp.obj`와 기존 PCH 의존성 파일에는 VS2026 `14.50.35717` 경로가 남아 있다.
- 프로젝트 범위 `Saved/UnrealBuildTool/BuildConfiguration.xml`에서 UBA executor를 비활성화하고, `-NoUBA` Live Coding 빌드로 오브젝트를 VS2022 기준으로 다시 생성한다.

## 캐시 우회 컴파일 결과

- 실행: `Build.bat ... -LiveCoding ... -NoUBA`
- 결과: 성공. `SharedPCH`와 `Module.Park3D.cpp`가 모두 `[NoUba]`로 다시 컴파일됐다.
- 툴체인: Visual Studio 2022 `14.44.35207`.
- 다음 단계: 에디터 `Ctrl+Alt+F11`으로 새 오브젝트의 패치 링크·적용 결과를 확인한다.
