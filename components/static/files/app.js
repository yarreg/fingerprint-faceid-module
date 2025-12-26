const API_BASE = '/api';

// Shared notification functions
function showSuccess(message) {
    const successEl = document.createElement('div');
    successEl.className = 'success';
    successEl.textContent = message;
    document.body.prepend(successEl);
    setTimeout(() => successEl.remove(), 3000);
}

function showError(message) {
    const errorEl = document.createElement('div');
    errorEl.className = 'error';
    errorEl.textContent = message;
    document.body.prepend(errorEl);
    setTimeout(() => errorEl.remove(), 3000);
}

// Shared API functionality
async function fetchWithAuth(url, options = {}) {
    const response = await fetch(url, {
        ...options,
        headers: {
            ...options.headers,
            ...getAuthHeaders()
        }
    });
    return handleApiResponse(response);
}

async function handleApiResponse(response) {
    if (!response.ok) {
        let error;
        try {
            error = await response.json();
        } catch (e) {
            error = {message: `HTTP error ${response.status}`};
        }
        throw new Error(error.message || 'API request failed');
    }
    return response.json();
}

// Authentication handling
function getAuthHeaders() {
    const auth = localStorage.getItem('auth');
    return auth ? { 'Authorization': `Basic ${auth}` } : {};
}
