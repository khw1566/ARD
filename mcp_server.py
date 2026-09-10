from mcp.server.fastmcp import FastMCP
import requests
import json
import sys

# ARD 프로젝트 전용 외부 MCP 서버
# Cursor IDE나 Claude Desktop에 이 서버를 연동하면, IDE 내부의 AI가 로봇의 상태를 읽고 제어할 수 있습니다.
# 설치: pip install mcp requests

mcp = FastMCP("ARD_Robot_MCP")
BASE_URL = "http://localhost:5000"
MCP_HEADERS = {"X-Client": "ARD_MCP"}

@mcp.resource("ard://system/status")
def get_system_status_resource() -> str:
    """현재 ARD 로봇 시스템의 모든 상태(온도, 모터 각도, 연결 상태 등)를 가져옵니다."""
    try:
        res = requests.get(f"{BASE_URL}/api/status", headers=MCP_HEADERS, timeout=2)
        if res.status_code == 200:
            return json.dumps(res.json(), indent=2, ensure_ascii=False)
        return f"Error: HTTP {res.status_code}"
    except Exception as e:
        return f"Connection Error: {e}"

@mcp.tool()
def get_robot_status() -> str:
    """현재 연결된 모든 MCU 기기의 온라인 상태와 모터 각도(좌표) 정보를 반환합니다."""
    return get_system_status_resource()

@mcp.tool()
def control_drive_motor(command: str) -> str:
    """DriveMCU 모터를 제어합니다 (이동 명령어)."""
    try:
        res = requests.post(f"{BASE_URL}/api/drivemcu/command", json={"command": command}, headers=MCP_HEADERS, timeout=2)
        return f"Command '{command}' sent." if res.status_code == 200 else f"Error: {res.text}"
    except Exception as e:
        return f"Connection Error: {e}"

@mcp.tool()
def control_pneumatic_gripper(action: str) -> str:
    """공압 그리퍼를 제어합니다 (action: 'open' 또는 'close')."""
    try:
        res = requests.post(f"{BASE_URL}/api/robot3axis/command", json={"command": f"grab {action}"}, headers=MCP_HEADERS, timeout=2)
        return f"Gripper {action} command sent." if res.status_code == 200 else f"Error: {res.text}"
    except Exception as e:
        return f"Connection Error: {e}"

@mcp.tool()
def trigger_image_sensor() -> str:
    """이미지 센서를 작동시켜 사진을 캡쳐하고 분석합니다."""
    try:
        res = requests.post(f"{BASE_URL}/api/imagesendermcu/command", json={"command": "capture"}, headers=MCP_HEADERS, timeout=2)
        return "Capture command sent." if res.status_code == 200 else f"Error: {res.text}"
    except Exception as e:
        return f"Connection Error: {e}"

@mcp.tool()
def move_3axis_robot(x_deg: float, y_mm: float, z_mm: float, absolute: bool = True) -> str:
    """3축 로봇을 지정된 프리셋 절대 좌표로 이동합니다."""
    try:
        payload = {"command": "move", "x": x_deg, "y": y_mm, "z": z_mm, "absolute": absolute}
        res = requests.post(f"{BASE_URL}/api/robot3axis/command", json=payload, headers=MCP_HEADERS, timeout=2)
        return f"Moved 3-axis robot to {x_deg}, {y_mm}, {z_mm}." if res.status_code == 200 else f"Error: {res.text}"
    except Exception as e:
        return f"Connection Error: {e}"

@mcp.tool()
def homing_all_robots() -> str:
    """모든 보드(로봇)를 원점으로 복귀시킵니다."""
    try:
        # DriveMCU Homing
        requests.post(f"{BASE_URL}/api/drivemcu/command", json={"command": "CMD_HOME"}, headers=MCP_HEADERS, timeout=2)
        # Robot3AxisMCU Homing
        requests.post(f"{BASE_URL}/api/robot3axis/command", json={"command": "home"}, headers=MCP_HEADERS, timeout=2)
        return "Homing command sent to all robots."
    except Exception as e:
        return f"Connection Error: {e}"

if __name__ == "__main__":
    print("ARD Robot MCP Server Started. (Use with Cursor IDE or Claude Desktop)")
    try:
        mcp.run()
    except Exception as e:
        print(f"Server Error: {e}")
        sys.exit(1)
