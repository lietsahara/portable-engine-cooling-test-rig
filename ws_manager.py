from typing import List, Optional
from fastapi import WebSocket
from fastapi.encoders import jsonable_encoder
import uuid

class ConnectionManager:
    def __init__(self):
        self.dashboard_connections: List[WebSocket] = []
        self.esp32_connection: Optional[WebSocket] = None

    async def connect_dashboard(self, websocket: WebSocket):
        await websocket.accept()
        self.dashboard_connections.append(websocket)

    def disconnect_dashboard(self, websocket: WebSocket):
        if websocket in self.dashboard_connections:
            self.dashboard_connections.remove(websocket)

    async def connect_esp32(self, websocket: WebSocket):
        await websocket.accept()
        self.esp32_connection = websocket

    def disconnect_esp32(self):
        self.esp32_connection = None

    async def broadcast(self, data: dict):
        encoded = jsonable_encoder(data)
        for connection in self.dashboard_connections:
            try:
                await connection.send_json(encoded)
            except Exception:
                self.disconnect_dashboard(connection)

    async def send_command_to_esp32(self, command: dict) -> dict:
        if not self.esp32_connection:
            return {"status": "error", "message": "ESP32 not connected"}
        command_id = str(uuid.uuid4())
        command["command_id"] = command_id
        await self.esp32_connection.send_json(command)
        return {"status": "sent", "command_id": command_id}

manager = ConnectionManager()