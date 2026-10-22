from pydantic import BaseModel
from typing import Optional

class Esp32Data(BaseModel):
    radiatorInlet: float
    radiatorOutlet: float
    engineInlet: float
    ambientTemp: Optional[float] = None
    rpm: Optional[float] = None
    fanSpeed: Optional[float] = None
    flowRate: Optional[float] = None
    efficiency: Optional[float] = None
    coolingRate: Optional[float] = None
    heatDissipation: Optional[float] = None
    tempRateOfChange: Optional[float] = None
    radiatorTempDiff: Optional[float] = None