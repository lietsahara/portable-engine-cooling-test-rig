from datetime import datetime, timezone
from typing import Optional
from sqlmodel import Field, SQLModel

class EngineDataBase(SQLModel):
    radiator_inlet: float = Field(index=True)
    radiator_outlet: float = Field(index=True)
    engine_inlet: float = Field(index=True)
    ambient_temp: Optional[float] = Field(default=None, index=True)
    rpm: Optional[float] = Field(default=None, index=True)
    fan_speed: Optional[float] = Field(default=None, index=True)
    flow_rate: Optional[float] = Field(default=None, index=True)
    efficiency: Optional[float] = Field(default=None, index=True)
    cooling_rate: Optional[float] = Field(default=None, index=True)
    heat_dissipation: Optional[float] = Field(default=None, index=True)
    temp_rate_of_change: Optional[float] = Field(default=None, index=True)
    radiator_temp_diff: Optional[float] = Field(default=None, index=True)
    timestamp: datetime = Field(default_factory=lambda: datetime.now(timezone.utc), index=True)

class EngineData(EngineDataBase, table=True):
    id: int | None = Field(default=None, primary_key=True)

class EnginePublic(EngineDataBase):
    id: int
    timestamp: datetime
