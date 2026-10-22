async function login() {
    const password = document.getElementById('admin-password').value;
    try {
        const res = await apiRequest('/api/admin/login', {
            method: 'POST',
            body: JSON.stringify({ password }),
        });
        setToken(res.token);
        document.getElementById('login-section').style.display = 'none';
        document.getElementById('admin-panel').style.display = 'block';
    } catch (e) {
        document.getElementById('login-error').textContent = 'Login failed';
    }
}

async function adminAction(path, body = {}) {
    const token = getToken();
    if (!token) return alert('Not logged in');
    try {
        const res = await apiRequest(path, {
            method: 'POST',
            headers: { 'Authorization': `Bearer ${token}` },
            body: JSON.stringify(body),
        });
        document.getElementById('admin-message').textContent = JSON.stringify(res);
    } catch (e) {
        document.getElementById('admin-message').textContent = 'Error: ' + e.message;
    }
}

function startRecording() { adminAction('/api/admin/recording/start'); }
function pauseRecording() { adminAction('/api/admin/recording/pause'); }
function stopRecording() { adminAction('/api/admin/recording/stop'); }
function newProject() {
    const name = document.getElementById('project-name').value;
    const save_sd = document.getElementById('save-sd').checked;
    const save_db = document.getElementById('save-db').checked;
    adminAction('/api/admin/project/new', { name, save_sd, save_db });
}
function restartEsp() { adminAction('/api/admin/ota/restart'); }
function switchMode(mode) { adminAction('/api/admin/ota/mode', { mode }); }
function updateNetwork() {
    const ap_ssid = document.getElementById('ap-ssid').value;
    const ap_password = document.getElementById('ap-password').value;
    const sta_ssid = document.getElementById('sta-ssid').value;
    const sta_password = document.getElementById('sta-password').value;
    adminAction('/api/admin/network/config', { ap_ssid, ap_password, sta_ssid, sta_password });
}
function changePassword() {
    const old_password = document.getElementById('old-password').value;
    const new_password = document.getElementById('new-password').value;
    adminAction('/api/admin/change-password', { old_password, new_password });
}

// Check if already logged in
if (getToken()) {
    document.getElementById('login-section').style.display = 'none';
    document.getElementById('admin-panel').style.display = 'block';
}