# =============================================================
# Park3D 저장소 SVN 등록 스크립트
# 실행 위치: 워킹카피 루트 (d:\Work\Unreal\Project\Parking)
# 전제: 이 폴더가 이미 `svn checkout`으로 저장소와 연결된 워킹카피여야 함.
#       아직이면 아래 "최초 연결" 주석을 참고.
# =============================================================

# --- (최초 연결이 필요할 때만) --------------------------------
# 빈 저장소 URL이 있다면, 상위 폴더에서 아래처럼 워킹카피로 만든 뒤
# 이 폴더 내용을 옮기거나, 직접 이 폴더를 워킹카피로 checkout 한다.
#   svn checkout <REPO_URL> .
# --------------------------------------------------------------

$ErrorActionPreference = "Stop"

# 1) 재귀 전역 무시 패턴 (SVN 1.8+ 상속 프로퍼티) - 빌드 산물/캐시
$global = @(
    "__pycache__"
    "*.pyc"
    "*.egg-info"
    ".venv"
    ".vs"
    "Binaries"
    "Intermediate"
    "DerivedDataCache"
) -join "`n"
svn propset svn:global-ignores $global .

# 2) Park3D 전용 폴더/파일 무시 (이름 고정 항목)
$park = @(
    "Binaries"
    "Intermediate"
    "DerivedDataCache"
    "Saved"
    "Save"
    ".vs"
    "*.sln"
    "*.slnx"
) -join "`n"
svn propset svn:ignore $park Park3D

# 3) 루트에서 .git 무시 (Git 메타데이터는 SVN에 넣지 않음)
svn propset svn:ignore ".git" .

# 4) 무시 규칙을 지킨 채 나머지 전부 추가
svn add --force . --depth infinity

# 5) 커밋
svn commit -m "Initial import: Park3D UE5 project + unreal-mcp + unity sources"

Write-Host "완료. `svn status` 로 결과를 확인하세요." -ForegroundColor Green
