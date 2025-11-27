/**
 * F.R.I.D.A.Y. RDP Client - Frontend Application
 * 
 * Handles UI interactions and communicates with the C++ backend via WebUI bindings.
 */

// ============================================================================
// DOM Elements
// ============================================================================
const elements = {
    // Form
    connectionForm: document.getElementById('connectionForm'),
    hostname: document.getElementById('hostname'),
    port: document.getElementById('port'),
    username: document.getElementById('username'),
    domain: document.getElementById('domain'),
    connectBtn: document.getElementById('connectBtn'),
    saveBtn: document.getElementById('saveBtn'),
    
    // Connections list
    connectionsList: document.getElementById('connectionsList'),
    
    // Modal
    saveModal: document.getElementById('saveModal'),
    connectionName: document.getElementById('connectionName'),
    modalClose: document.getElementById('modalClose'),
    modalCancel: document.getElementById('modalCancel'),
    modalSave: document.getElementById('modalSave'),
    
    // Other
    status: document.getElementById('status'),
    appInfo: document.getElementById('appInfo'),
    toastContainer: document.getElementById('toastContainer'),
};

// ============================================================================
// State
// ============================================================================
let connections = [];
let selectedConnection = null;

// ============================================================================
// Toast Notifications
// ============================================================================
function showToast(message, type = 'info', duration = 4000) {
    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    toast.innerHTML = `
        <span>${message}</span>
    `;
    
    elements.toastContainer.appendChild(toast);
    
    setTimeout(() => {
        toast.classList.add('toast-out');
        setTimeout(() => toast.remove(), 300);
    }, duration);
}

// ============================================================================
// Status Updates
// ============================================================================
function updateStatus(text, isOnline = true) {
    const statusDot = elements.status.querySelector('.status-dot');
    const statusText = elements.status.querySelector('.status-text');
    
    statusText.textContent = text;
    statusDot.style.background = isOnline ? 'var(--color-success)' : 'var(--color-warning)';
    statusDot.style.boxShadow = isOnline 
        ? '0 0 8px var(--color-success)' 
        : '0 0 8px var(--color-warning)';
}

// ============================================================================
// Modal Management
// ============================================================================
function openSaveModal() {
    // Pre-fill with hostname if empty
    if (!elements.connectionName.value && elements.hostname.value) {
        elements.connectionName.value = elements.hostname.value;
    }
    elements.saveModal.classList.add('active');
    elements.connectionName.focus();
}

function closeSaveModal() {
    elements.saveModal.classList.remove('active');
    elements.connectionName.value = '';
}

// ============================================================================
// Connections List
// ============================================================================
function renderConnections() {
    if (connections.length === 0) {
        elements.connectionsList.innerHTML = `
            <div class="empty-state">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <circle cx="12" cy="12" r="10"/>
                    <path d="M8 14s1.5 2 4 2 4-2 4-2"/>
                    <line x1="9" y1="9" x2="9.01" y2="9"/>
                    <line x1="15" y1="9" x2="15.01" y2="9"/>
                </svg>
                <p>No saved connections yet</p>
                <p class="empty-hint">Fill in the form above and click Save</p>
            </div>
        `;
        return;
    }
    
    elements.connectionsList.innerHTML = connections.map((conn, index) => `
        <div class="connection-item ${selectedConnection === index ? 'selected' : ''}" 
             data-index="${index}">
            <div class="connection-icon">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <rect x="2" y="3" width="20" height="14" rx="2" ry="2"/>
                    <line x1="8" y1="21" x2="16" y2="21"/>
                    <line x1="12" y1="17" x2="12" y2="21"/>
                </svg>
            </div>
            <div class="connection-info">
                <div class="connection-name">${escapeHtml(conn.name)}</div>
                <div class="connection-host">${escapeHtml(conn.hostname)}:${conn.port}</div>
            </div>
            <div class="connection-actions">
                <button class="btn btn-danger delete-btn" data-name="${escapeHtml(conn.name)}" title="Delete">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                        <polyline points="3 6 5 6 21 6"/>
                        <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>
                    </svg>
                </button>
            </div>
        </div>
    `).join('');
    
    // Add event listeners
    elements.connectionsList.querySelectorAll('.connection-item').forEach(item => {
        item.addEventListener('click', (e) => {
            // Don't trigger if clicking delete button
            if (e.target.closest('.delete-btn')) return;
            
            const index = parseInt(item.dataset.index);
            selectConnection(index);
        });
        
        item.addEventListener('dblclick', (e) => {
            if (e.target.closest('.delete-btn')) return;
            const index = parseInt(item.dataset.index);
            selectConnection(index);
            handleConnect();
        });
    });
    
    elements.connectionsList.querySelectorAll('.delete-btn').forEach(btn => {
        btn.addEventListener('click', (e) => {
            e.stopPropagation();
            const name = btn.dataset.name;
            handleDeleteConnection(name);
        });
    });
}

function selectConnection(index) {
    selectedConnection = index;
    const conn = connections[index];
    
    // Fill form with connection details
    elements.hostname.value = conn.hostname;
    elements.port.value = conn.port;
    elements.username.value = conn.username || '';
    elements.domain.value = conn.domain || '';
    
    renderConnections();
}

// ============================================================================
// Backend Communication
// ============================================================================
async function loadConnections() {
    try {
        const result = await webui.call('getConnections');
        connections = JSON.parse(result);
        renderConnections();
    } catch (error) {
        console.error('Failed to load connections:', error);
        showToast('Failed to load saved connections', 'error');
    }
}

async function loadAppInfo() {
    try {
        const result = await webui.call('getAppInfo');
        const info = JSON.parse(result);
        elements.appInfo.textContent = `${info.name} v${info.version} | FreeRDP ${info.freerdp_version}`;
    } catch (error) {
        console.error('Failed to load app info:', error);
    }
}

async function handleConnect() {
    const hostname = elements.hostname.value.trim();
    const port = parseInt(elements.port.value) || 3389;
    const username = elements.username.value.trim();
    const domain = elements.domain.value.trim();
    
    if (!hostname) {
        showToast('Please enter a hostname or IP address', 'error');
        elements.hostname.focus();
        return;
    }
    
    updateStatus('Connecting...', false);
    elements.connectBtn.disabled = true;
    
    try {
        const result = await webui.call('connectRDP', hostname, port, username, domain);
        const response = JSON.parse(result);
        
        if (response.success) {
            showToast(`Connecting to ${hostname}...`, 'success');
            updateStatus('Session Active', true);
        } else {
            showToast(`Connection failed: ${response.error}`, 'error');
            updateStatus('Connection Failed', false);
            setTimeout(() => updateStatus('Systems Online', true), 3000);
        }
    } catch (error) {
        console.error('Connect error:', error);
        showToast('Failed to initiate connection', 'error');
        updateStatus('Error', false);
        setTimeout(() => updateStatus('Systems Online', true), 3000);
    } finally {
        elements.connectBtn.disabled = false;
    }
}

async function handleSaveConnection() {
    const name = elements.connectionName.value.trim();
    const hostname = elements.hostname.value.trim();
    const port = parseInt(elements.port.value) || 3389;
    const username = elements.username.value.trim();
    const domain = elements.domain.value.trim();
    
    if (!name) {
        showToast('Please enter a connection name', 'error');
        elements.connectionName.focus();
        return;
    }
    
    if (!hostname) {
        showToast('Please enter a hostname first', 'error');
        closeSaveModal();
        elements.hostname.focus();
        return;
    }
    
    try {
        const success = await webui.call('saveConnection', name, hostname, port, username, domain);
        
        if (success) {
            showToast(`Connection "${name}" saved`, 'success');
            closeSaveModal();
            await loadConnections();
        } else {
            showToast('Failed to save connection', 'error');
        }
    } catch (error) {
        console.error('Save error:', error);
        showToast('Failed to save connection', 'error');
    }
}

async function handleDeleteConnection(name) {
    if (!confirm(`Delete connection "${name}"?`)) {
        return;
    }
    
    try {
        const success = await webui.call('deleteConnection', name);
        
        if (success) {
            showToast(`Connection "${name}" deleted`, 'info');
            selectedConnection = null;
            await loadConnections();
        } else {
            showToast('Failed to delete connection', 'error');
        }
    } catch (error) {
        console.error('Delete error:', error);
        showToast('Failed to delete connection', 'error');
    }
}

// ============================================================================
// Utility Functions
// ============================================================================
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// ============================================================================
// Event Listeners
// ============================================================================
function initEventListeners() {
    // Form submission
    elements.connectionForm.addEventListener('submit', (e) => {
        e.preventDefault();
        handleConnect();
    });
    
    // Save button
    elements.saveBtn.addEventListener('click', () => {
        openSaveModal();
    });
    
    // Modal controls
    elements.modalClose.addEventListener('click', closeSaveModal);
    elements.modalCancel.addEventListener('click', closeSaveModal);
    elements.modalSave.addEventListener('click', handleSaveConnection);
    
    // Modal backdrop click
    elements.saveModal.addEventListener('click', (e) => {
        if (e.target === elements.saveModal) {
            closeSaveModal();
        }
    });
    
    // Keyboard shortcuts
    document.addEventListener('keydown', (e) => {
        // Escape to close modal
        if (e.key === 'Escape' && elements.saveModal.classList.contains('active')) {
            closeSaveModal();
        }
        
        // Enter in modal to save
        if (e.key === 'Enter' && elements.saveModal.classList.contains('active')) {
            e.preventDefault();
            handleSaveConnection();
        }
        
        // Ctrl+S to save
        if (e.ctrlKey && e.key === 's') {
            e.preventDefault();
            if (elements.hostname.value.trim()) {
                openSaveModal();
            }
        }
        
        // Ctrl+Enter to connect
        if (e.ctrlKey && e.key === 'Enter') {
            e.preventDefault();
            handleConnect();
        }
    });
}

// ============================================================================
// Initialization
// ============================================================================
async function init() {
    console.log('[FRIDAY] Initializing interface...');
    
    initEventListeners();
    await loadAppInfo();
    await loadConnections();
    
    // Focus hostname field
    elements.hostname.focus();
    
    console.log('[FRIDAY] Interface ready, Boss.');
}

// Start when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
