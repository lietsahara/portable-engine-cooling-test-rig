const API_BASE = '';

async function apiRequest(path, options = {}) {
    const { headers, ...rest } = options;
    const res = await fetch(API_BASE + path, {
        ...rest,
        credentials: 'include',
        headers: {
            'Content-Type': 'application/json',
            ...(headers || {}),
        },
    });
    if (!res.ok) throw new Error(await res.text());
    return res.json();
}

function getToken() { return localStorage.getItem('admin_token'); }
function setToken(token) { localStorage.setItem('admin_token', token); }
function clearToken() { localStorage.removeItem('admin_token'); }