import time

class SSEManager:
    """Manages Server-Sent Events (SSE) clients and broadcasting."""
    def __init__(self):
        self.clients = []

    def add_client(self, client_queue):
        self.clients.append(client_queue)

    def remove_client(self, client_queue):
        if client_queue in self.clients:
            self.clients.remove(client_queue)

    def broadcast_event(self, data: dict):
        """Broadcasts a generic JSON event to all connected clients."""
        for q in self.clients:
            q.append(data)

    def broadcast_system_log(self, msg: str, source: str = "System"):
        """Broadcasts a system log message to all connected clients and prints to console."""
        # 200 HTTP 성공 로그 무시
        msg_str = str(msg)
        if " 200 " in msg_str or " 200 -" in msg_str or msg_str.strip().endswith(" 200"):
            return

        print(f"[{source}] {msg}")
        for q in self.clients:
            q.append({
                "type": "log",
                "log": {
                    "timestamp": time.strftime("%H:%M:%S"),
                    "source": source,
                    "message": msg
                }
            })

# Create a singleton instance
sse_manager = SSEManager()
