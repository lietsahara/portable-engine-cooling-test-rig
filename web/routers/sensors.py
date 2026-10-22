from typing import Annotated
from fastapi import APIRouter, HTTPException, Query
from sqlmodel import select

from database import SessionDep
from models import EngineData, EnginePublic
from schemas import Esp32Data
from ws_manager import manager

router = APIRouter(prefix="/api/sensor-data", tags=["sensor-data"])

@router.post("/", response_model=EnginePublic)
async def create_sensor_data(payload: Esp32Data, session: SessionDep):
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
    return db_data

@router.get("/latest", response_model=EnginePublic)
def get_latest(session: SessionDep):
    statement = select(EngineData).order_by(EngineData.timestamp.desc()).limit(1)
    result = session.exec(statement).first()
    if not result:
        raise HTTPException(status_code=404, detail="No data yet")
    return result

@router.get("/", response_model=list[EnginePublic])
def list_sensor_data(
    session: SessionDep,
    offset: int = 0,
    limit: Annotated[int, Query(le=100)] = 100,
):
    statement = select(EngineData).offset(offset).limit(limit)
    return session.exec(statement).all()