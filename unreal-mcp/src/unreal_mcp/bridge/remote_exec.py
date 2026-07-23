"""언리얼 Python Remote Execution 프로토콜 클라이언트.

PythonScriptPlugin의 remote_execution 프로토콜(버전 1, magic "ue_py") 구현:
  1. UDP 멀티캐스트(239.0.0.1:6766)로 ping → 에디터가 pong (노드 발견)
  2. 클라이언트가 TCP 리슨 소켓을 열고 open_connection 메시지 전송 → 에디터가 접속
  3. TCP 채널로 command(ExecuteFile) 전송, command_result 수신

DefaultEngine.ini의 RemoteExecutionMulticastBindAddress=127.0.0.1 설정과 짝을 이루도록
모든 소켓을 127.0.0.1에 바인딩한다 (로컬 전용).
"""

import json
import logging
import os
import socket
import struct
import threading
import time
import uuid

_log = logging.getLogger("unreal_mcp.bridge")

PROTOCOL_VERSION = 1
MAGIC = "ue_py"
MULTICAST_GROUP = ("239.0.0.1", 6766)
BIND_ADDRESS = os.environ.get("UE_MCP_BIND", "127.0.0.1")
COMMAND_IP = "127.0.0.1"


class EditorNotRunning(RuntimeError):
    pass


class BridgeError(RuntimeError):
    pass


def _encode(msg_type: str, source: str, dest: str | None = None, data: dict | None = None) -> bytes:
    msg: dict = {"version": PROTOCOL_VERSION, "magic": MAGIC, "type": msg_type, "source": source}
    if dest is not None:
        msg["dest"] = dest
    if data is not None:
        msg["data"] = data
    return json.dumps(msg).encode("utf-8")


def _decode(raw: bytes, expect_dest: str | None) -> dict | None:
    try:
        msg = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return None
    if msg.get("magic") != MAGIC or msg.get("version") != PROTOCOL_VERSION:
        return None
    dest = msg.get("dest")
    if dest is not None and expect_dest is not None and dest != expect_dest:
        return None
    return msg


class _Connection:
    def __init__(self) -> None:
        self.node_id = str(uuid.uuid4())
        self.remote_id: str | None = None
        self._udp: socket.socket | None = None
        self._cmd: socket.socket | None = None

    def open(self, discover_timeout: float = 2.0) -> None:
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        udp.bind((BIND_ADDRESS, MULTICAST_GROUP[1]))
        mreq = struct.pack(
            "=4s4s", socket.inet_aton(MULTICAST_GROUP[0]), socket.inet_aton(BIND_ADDRESS)
        )
        udp.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        udp.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
        udp.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 0)
        udp.settimeout(0.25)
        self._udp = udp

        deadline = time.monotonic() + discover_timeout
        udp.sendto(_encode("ping", self.node_id), MULTICAST_GROUP)
        remote: str | None = None
        while remote is None and time.monotonic() < deadline:
            try:
                raw, _addr = udp.recvfrom(8192)
            except socket.timeout:
                udp.sendto(_encode("ping", self.node_id), MULTICAST_GROUP)
                continue
            msg = _decode(raw, self.node_id)
            if msg and msg.get("type") == "pong":
                remote = msg.get("source")
        if remote is None:
            self.close()
            raise EditorNotRunning(
                "에디터를 찾지 못했습니다 (Python Remote Execution이 켜진 에디터가 실행 중인지 확인)"
            )
        self.remote_id = remote

        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM, socket.IPPROTO_TCP)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((COMMAND_IP, 0))
        listener.listen(1)
        listener.settimeout(3.0)
        port = listener.getsockname()[1]
        udp.sendto(
            _encode(
                "open_connection",
                self.node_id,
                remote,
                {"command_ip": COMMAND_IP, "command_port": port},
            ),
            MULTICAST_GROUP,
        )
        try:
            conn, _addr = listener.accept()
        except socket.timeout:
            self.close()
            raise EditorNotRunning("에디터가 명령 채널을 열지 않았습니다")
        finally:
            listener.close()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._cmd = conn
        _log.info("에디터 연결됨 (node %s...)", (self.remote_id or "")[:8])

    def run(self, code: str, timeout: float) -> dict:
        """code를 ExecuteFile 모드로 실행하고 command_result의 data를 반환."""
        if self._cmd is None:
            raise BridgeError("connection not open")
        payload = _encode(
            "command",
            self.node_id,
            self.remote_id,
            {"command": code, "unattended": True, "exec_mode": "ExecuteFile"},
        )
        self._cmd.sendall(payload)
        self._cmd.settimeout(0.5)
        deadline = time.monotonic() + timeout
        buf = b""
        while True:
            if time.monotonic() > deadline:
                raise TimeoutError(f"editor command timed out after {timeout}s")
            try:
                chunk = self._cmd.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                raise BridgeError("command channel closed by editor")
            buf += chunk
            try:
                msg = json.loads(buf.decode("utf-8"))
            except (UnicodeDecodeError, ValueError):
                continue  # 메시지가 아직 다 안 옴
            buf = b""
            if msg.get("type") == "command_result":
                return msg.get("data") or {}

    def close(self) -> None:
        if self._udp is not None and self.remote_id is not None:
            try:
                self._udp.sendto(
                    _encode("close_connection", self.node_id, self.remote_id), MULTICAST_GROUP
                )
            except OSError:
                pass
        for sock in (self._cmd, self._udp):
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
        self._cmd = None
        self._udp = None


_lock = threading.Lock()
_conn: _Connection | None = None


def _drop_locked() -> None:
    global _conn
    if _conn is not None:
        _conn.close()
        _conn = None


def execute(code: str, timeout: float = 30.0) -> dict:
    """스니펫 실행. 연결은 캐시하고 끊기면 1회 재연결 후 재시도."""
    global _conn
    with _lock:
        last_err: Exception | None = None
        for attempt in (1, 2):
            if _conn is None:
                _log.info("에디터 탐색 중 (UDP 멀티캐스트 %s:%s)...", *MULTICAST_GROUP)
                conn = _Connection()
                conn.open()
                _conn = conn
            try:
                return _conn.run(code, timeout)
            except TimeoutError:
                _drop_locked()  # 늦게 도착할 응답으로 채널이 어긋나지 않도록 폐기
                raise
            except (BridgeError, OSError) as e:
                last_err = e
                _drop_locked()
        raise BridgeError(str(last_err))


def reset() -> None:
    with _lock:
        _drop_locked()
