from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from sqlmodel import Session

from database import engine
from models import EngineData
from schemas import Esp32Data
from ws_manager import manager

router = APIRouter(tags=["websocket"])

@router.websocket("/ws/esp32")
async def websocket_esp32(websocket: WebSocket):
    await manager.connect_esp32(websocket)
    try:
        while True:
            data = await websocket.receive_json()
            payload = Esp32Data(**data)

            with Session(engine) as session:
                db_data = EngineData(
                    radiator_inlet=payload.radiatorInlet,
                    radiator_outlet=payload.radiatorOutlet,
                    engine_inlet=payload.engineInlet,
                    ambient_temp=payload.ambientTemp,
                    rpm=payload.rpm,
                    fan_speed=payload.fanSpeed,
                    flow_rate=payload.flowRate,
                    efficiency=payload.efficiency,
                    cooling_rate=payload.coolingRate,
                    heat_dissipation=payload.heatDissipation,
                    temp_rate_of_change=payload.tempRateOfChange,
                    radiator_temp_diff=payload.radiatorTempDiff,
                )
                session.add(db_data)
                session.commit()
                session.refresh(db_data)
                await manager.broadcast(db_data.model_dump())
                await websocket.send_json({"status": "saved", "id": db_data.id})
    except WebSocketDisconnect:
        manager.disconnect_esp32()

@router.websocket("/ws/dashboard")
async def websocket_dashboard(websocket: WebSocket):
    await manager.connect_dashboard(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect_dashboard(websocket)