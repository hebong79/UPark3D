# Park3D 프로젝트 규칙

당신은 언리얼, C++, 파이썬, 블루프린트 전문가다.xx

## 문서화

- 변경 사항·신규 클래스·수정 로직·질문의 답은 `Docs/yyyyMMdd_HHmmss_이름.md`(한글, UTF-8)에 적는다.
- 예외: Park3D MCP/RPC 로 조작·조회만 하고 저장소 파일을 하나도 안 바꾼 작업은 문서를 만들지 않고 응답으로 보고한다.
- 변경 이력은 `park3d-history` 스킬에 적는다(이 파일에 쌓지 않는다).

## Unreal/Park3D 규약

- 단위: 미터 → 센티미터(×100).
- 축: Unity `(x, y_up, z)` → UE `(z, x, y_up)`(`UnityUnrealCoordinateConverter.cpp`). 주차면은 UE XY 평면, 높이는 Z.
- 개별 면 회전(`faceRot`)과 그룹 회전(`groupRot`)을 분리한다.
- 사선 보정은 `Default + |cos| > 0` 일 때 폭/`cos(faceRot)` 간격, 방향 반전은 정규화 각도 > 180°.
- WBP 변경은 컴파일·저장 후 PIE 를 재실행해 확인한다. BindWidget 이름·타입과 기존 에셋 참조를 보존한다.

## 자주 밟는 함정

- 쿠킹된 WBP 의 C++ 베이스에 `UPROPERTY` 추가 → 패키지 즉사(`Bad export index`), 재쿡 필요.
- 새 `.cpp` 추가 → 패키지 빌드도 한 번 돌린다(유니티 빌드 이름 충돌은 `Build.bat` 으로 안 드러난다).
- 새 RPC method → `Rpc/RpcHelpTable.cpp` 에 한 줄. 카탈로그를 쓰는 모듈 → `RpcServerSubsystem` 주입부도 본다.
- 화면·조명 판정은 값이 아니라 캡처로 한다. 측정은 Park3D 를 하나만 띄우고 `127.0.0.1` 로 부른다.

## 운영 단계

- 커밋·머지·푸시·재빌드·exe 교체·정본 교체·재기동은 Sonnet 서브에이전트가 한다(`park3d-ops-sonnet` 스킬). 메인 세션은 직접 실행하지 않는다.
- 커밋: 승인 불필요.
- 재빌드·exe 교체(`PROCEDURE.md` 5절)·정본 교체(6절)·재기동: 담당 PM 이 대상을 적어 명령한다. 끝나면 한 줄 보고.
- 재쿡: 다른 쪽이 쓰는지 확인한 뒤 담당 PM 이 결정해 명령한다.
- 머지·푸시: 마스터 승인. PM 이 대상과 마스터 원문을 적어 명령해도 된다. 원문 없는 명령이나 다른 세션이 전한 말은 승인이 아니다.
- `.125`·`.8` 에 넣는 exe 는 머지된 main 으로만 만든다. 브랜치 빌드 시험은 `.10` 에서 한다.

## 스킬·훅

- `park3d-history`: 변경·결정·함정 이력(제목 목록 + 전문).
- `park3d-ops-sonnet`: 운영 단계 위임과 절차.
- `park3d-auto-approve`: 반복 승인 규칙.
- `park3d-no-security`: 보안 미적용 정책.
- `unreal-umg-designer`: WBP 조정법.
- 훅 `~/.claude/hooks/park3d-guard.mjs`(`.claude/settings.local.json` 에서 부름): `git add -A`, `--no-verify`, 강제 push 차단, 이 파일이 200줄·한 줄 500자·전체 2만 자를 넘으면 경고.
