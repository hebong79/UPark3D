#!/usr/bin/env bash
# 베이크 서버(192.168.0.210, agent01) 구성 재현 스크립트. 서버에서 실행한다.
#   scp -i ~/.ssh/id_ed25519_park3d_bake sdf_core.py server.py park3d-plate-bake.service \
#       setup_server.sh agent01@192.168.0.210:~/park3d_plate_bake/
#   ssh -i ~/.ssh/id_ed25519_park3d_bake agent01@192.168.0.210 'bash ~/park3d_plate_bake/setup_server.sh'
#
# 보안 설정(방화벽·인증)은 일부러 건드리지 않는다 — `park3d-no-security` 정책.
set -euo pipefail

ROOT="$HOME/park3d_plate_bake"
VENV="$ROOT/venv"
mkdir -p "$ROOT"

# 1) venv
[ -d "$VENV" ] || python3 -m venv "$VENV"

# 2) torch(CUDA)는 이미 있는 vllm-env 것을 재사용한다.
#    새로 받으면 cu130 휠이 3GB 가까이 되고, 같은 머신에 같은 버전이 두 벌 남는다.
#    .pth 는 sys.path 뒤에 붙으므로 우리 venv 의 numpy/Pillow 가 우선한다.
VLLM_SP="$HOME/vllm-env/lib/python3.10/site-packages"
if [ -d "$VLLM_SP" ]; then
  echo "$VLLM_SP" > "$VENV/lib/python3.10/site-packages/_vllm_torch.pth"
else
  echo "[경고] $VLLM_SP 가 없다 — torch 를 직접 설치한다(수 GB)"
  "$VENV/bin/pip" install torch --index-url https://download.pytorch.org/whl/cu124
fi

# 3) 나머지 의존성. Pillow 는 **로컬과 같은 버전으로 못 박는다** —
#    래스터가 달라지면 로컬 산출물과의 수치 일치가 깨진다.
"$VENV/bin/pip" install -q --upgrade pip
"$VENV/bin/pip" install -q "Pillow==12.1.0" numpy fastapi uvicorn python-multipart

# 4) systemd user service (linger=yes 라 로그인 없이 부팅 때 뜬다)
mkdir -p "$HOME/.config/systemd/user"
cp "$ROOT/park3d-plate-bake.service" "$HOME/.config/systemd/user/"
systemctl --user daemon-reload
systemctl --user enable --now park3d-plate-bake

sleep 3
curl -s localhost:8090/health; echo
