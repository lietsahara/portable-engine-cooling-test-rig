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
    print("[ESP32 WS] handler started")
    try:
        while True:
            try:
                data = await websocket.receive_json()
                print(f"[ESP32 WS] got message, keys={list(data.keys())}")
            except Exception as e:
                print(f"[ESP32 WS] receive error: {type(e).__name__}: {e}")
                continue
            try:
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
                        cooling_rate=payload.coolingRate,
                        heat_dissipation=payload.heatDissipation,
                        temp_rate_of_change=payload.tempRateOfChange,
                        radiator_temp_diff=payload.radiatorTempDiff,
                    )
                    session.add(db_data)
                    session.commit()
                    session.refresh(db_data)
                    print(f"[ESP32 WS] stored id={db_data.id}, broadcasting")
                    await manager.broadcast(db_data.model_dump())
            except Exception as e:
                print(f"[ESP32 WS] processing error: {type(e).__name__}: {e}")
    except WebSocketDisconnect:
        print("[ESP32 WS] client disconnected")
        manager.disconnect_esp32()
    except Exception as e:
        print(f"[ESP32 WS] handler exception: {type(e).__name__}: {e}")

@router.websocket("/ws/dashboard")
async def websocket_dashboard(websocket: WebSocket):
    await manager.connect_dashboard(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect_dashboard(websocket)