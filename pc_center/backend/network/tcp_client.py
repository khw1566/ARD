import socket
import threading
import time
from network.sse_manager import sse_manager
from core.state_manager import state_manager

class TCPClient:
    """Handles sending asynchronous TCP commands to MCUs."""
    def __init__(self):
        pass

    def _send_tcp_worker(self, ip, port, command, mcu_name, retry_count=3):
        last_err = None
        for attempt in range(retry_count):
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                    s.settimeout(0.5)
                    s.connect((ip, port))
                    s.sendall((command + '\n').encode())
                
                # Update last seen
                state_manager.last_seen[mcu_name.lower()] = time.time()
                return
            except socket.timeout:
                last_err = "Timeout"
            except ConnectionRefusedError:
                last_err = "Connection Refused"
            except Exception as e:
                last_err = str(e)
                
            time.sleep(0.1)
            
        print(f"[TCP] {mcu_name} ({ip}) Failed to send: {command} (Error: {last_err})")
        sse_manager.broadcast_system_log(f"[{mcu_name}] 명령 전송 실패: {command} ({last_err})", "Network")

    def send_command_async(self, ip, port, command, mcu_name="MCU"):
        """Sends a TCP command asynchronously."""
        threading.Thread(target=self._send_tcp_worker, args=(ip, port, command, mcu_name), daemon=True).start()

# Create a singleton instance
tcp_client = TCPClient()

def send_tcp_command_async(ip, port, command, mcu_name="MCU"):
    """Helper function to keep legacy interface."""
    tcp_client.send_command_async(ip, port, command, mcu_name)
