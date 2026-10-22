from fastapi import APIRouter, Depends, HTTPException, Header
from pydantic import BaseModel
from typing import Optional
import hashlib, secrets

from ws_manager import manager

router = APIRouter(prefix="/api/admin", tags=["admin"])

ADMIN_PASSWORD_HASH = hashlib.sha256("admin".encode()).hexdigest()
active_tokens = set()

class LoginRequest(BaseModel):
    password: str

class ChangePasswordRequest(BaseModel):
    old_password: str
    new_password: str

class ProjectRequest(BaseModel):
    name: str
    save_sd: bool = True
    save_db: bool = True

class ModeRequest(BaseModel):
    mode: str

class NetworkRequest(BaseModel):
    ap_ssid: Optional[str] = None
    ap_password: Optional[str] = None
    sta_ssid: Optional[str] = None
    sta_password: Optional[str] = None

def verify_token(authorization: str = Header(...)):
    token = authorization.replace("Bearer ", "")
    if token not in active_tokens:
        raise HTTPException(status_code=401, detail="Unauthorized")
    return token

@router.post("/login")
def login(req: LoginRequest):
    global ADMIN_PASSWORD_HASH
    if hashlib.sha256(req.password.encode()).hexdigest() != ADMIN_PASSWORD_HASH:
        raise HTTPException(status_code=401, detail="Invalid password")
    token = secrets.token_urlsafe(32)
    active_tokens.add(token)
    return {"token": token}

@router.post("/change-password")
def change_password(req: ChangePasswordRequest, token: str = Depends(verify_token)):
    global ADMIN_PASSWORD_HASH
    if hashlib.sha256(req.old_password.encode()).hexdigest() != ADMIN_PASSWORD_HASH:
        raise HTTPException(status_code=401, detail="Old password incorrect")
    ADMIN_PASSWORD_HASH = hashlib.sha256(req.new_password.encode()).hexdigest()
    return {"status": "ok"}

@router.post("/recording/start")
async def start_recording(token: str = Depends(verify_token)):
    return await manager.send_command_to_esp32({"command": "recording_start"})

@router.post("/recording/pause")
async def pause_recording(token: str = Depends(verify_token)):
    return await manager.send_command_to_esp32({"command": "recording_pause"})

@router.post("/recording/stop")
async def stop_recording(token: str = Depends(verify_token)):
    return await manager.send_command_to_esp32({"command": "recording_stop"})

@router.post("/project/new")
async def new_project(req: ProjectRequest, token: str = Depends(verify_token)):
    return await manager.send_command_to_esp32({
        "command": "new_project",
        "name": req.name,
        "save_sd": req.save_sd,
        "save_db": req.save_db,
    })

@router.post("/ota/restart")
async def restart_esp(token: str = Depends(verify_token)):
    return await manager.send_command_to_esp32({"command": "restart"})

@router.post("/ota/mode")
async def switch_mode(req: ModeRequest, token: str = Depends(verify_token)):
    if req.mode not in ("ap", "sta"):
        raise HTTPException(status_code=400, detail="Mode must be 'ap' or 'sta'")
    return await manager.send_command_to_esp32({"command": "switch_mode", "mode": req.mode})

@router.post("/network/config")
async def update_network(req: NetworkRequest, token: str = Depends(verify_token)):
    return await manager.send_command_to_esp32({
        "command": "network_config",
        "ap_ssid": req.ap_ssid,
        "ap_password": req.ap_password,
        "sta_ssid": req.sta_ssid,
        "sta_password": req.sta_password,
    })