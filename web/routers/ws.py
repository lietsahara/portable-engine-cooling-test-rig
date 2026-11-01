from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from sqlmodel import Session

from database import engine
from models import EngineData
from schemas import Esp32Data
from ws_manager import manager

from starlette.websockets import WebSocketState
import json

router = APIRouter(tags=["websocket"])


@router.websocket("/ws/esp32")
async def websocket_esp32(websocket: WebSocket):
    await manager.connect_esp32(websocket)
    print("[ESP32 WS] handler started")
    try:
        while True:
            try:
                data = await websocket.receive_json()
            except json.JSONDecodeError as e:
                print(f"[ESP32 WS] bad JSON: {e}")
                continue
            except Exception as e:
                print(f"[ESP32 WS] disconnected: {type(e).__name__}: {e}")
                break

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
                    print(f"[ESP32 WS] stored id={db_data.id}")
                    await manager.broadcast(db_data.model_dump())
            except Exception as e:
                print(f"[ESP32 WS] processing error: {type(e).__name__}: {e}")
    except WebSocketDisconnect:
        pass
    finally:
        print("[ESP32 WS] client disconnected")
        manager.disconnect_esp32()

@router.websocket("/ws/dashboard")
async def websocket_dashboard(websocket: WebSocket):
    await manager.connect_dashboard(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect_dashboard(websocket)