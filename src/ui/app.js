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
    
    // Advanced Options
    advancedToggle: document.getElementById('advancedToggle'),
    advancedOptions: document.getElementById('advancedOptions'),
    optHomeDrive: document.getElementById('optHomeDrive'),
    optClipboard: document.getElementById('optClipboard'),
    optUsbAuto: document.getElementById('optUsbAuto'),
    optFloatbar: document.getElementById('optFloatbar'),
    optDynamicResolution: document.getElementById('optDynamicResolution'),
    optNetworkAuto: document.getElementById('optNetworkAuto'),
    optGfxAvc420: document.getElementById('optGfxAvc420'),
    optCompression: document.getElementById('optCompression'),
    optAudioPulse: document.getElementById('optAudioPulse'),
    optPreventLock: document.getElementById('optPreventLock'),
    optCertTofu: document.getElementById('optCertTofu'),
    optAutoReconnect: document.getElementById('optAutoReconnect'),
    optReconnectRetries: document.getElementById('optReconnectRetries'),
    reconnectRetriesGroup: document.getElementById('reconnectRetriesGroup'),
    
    // Connections list
    connectionsList: document.getElementById('connectionsList'),
    
    // Save Modal
    saveModal: document.getElementById('saveModal'),
    connectionName: document.getElementById('connectionName'),
    modalClose: document.getElementById('modalClose'),
    modalCancel: document.getElementById('modalCancel'),
    modalSave: document.getElementById('modalSave'),
    
    // Certificate Modal
    certModal: document.getElementById('certModal'),
    certModalTitle: document.getElementById('certModalTitle'),
    certWarning: document.getElementById('certWarning'),
    certHost: document.getElementById('certHost'),
    certCN: document.getElementById('certCN'),
    certSubject: document.getElementById('certSubject'),
    certIssuer: document.getElementById('certIssuer'),
    certFingerprint: document.getElementById('certFingerprint'),
    certOldFingerprintRow: document.getElementById('certOldFingerprintRow'),
    certOldFingerprint: document.getElementById('certOldFingerprint'),
    certReject: document.getElementById('certReject'),
    certAcceptTemp: document.getElementById('certAcceptTemp'),
    certAcceptPerm: document.getElementById('certAcceptPerm'),
    
    // Auth Modal
    authModal: document.getElementById('authModal'),
    authModalTitle: document.getElementById('authModalTitle'),
    authModalClose: document.getElementById('authModalClose'),
    authPrompt: document.getElementById('authPrompt'),
    authUsername: document.getElementById('authUsername'),
    authPassword: document.getElementById('authPassword'),
    authDomain: document.getElementById('authDomain'),
    authCancel: document.getElementById('authCancel'),
    authSubmit: document.getElementById('authSubmit'),
    
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
// Certificate Dialog (called from C++ backend)
// ============================================================================
function showCertificateDialog(certInfo) {
    console.log('[FRIDAY] Certificate verification requested:', certInfo);
    
    // Update the title based on whether certificate changed
    if (certInfo.isChanged) {
        elements.certModalTitle.textContent = 'Certificate Has Changed!';
        elements.certWarning.innerHTML = `
            <p><strong>WARNING:</strong> The server's certificate has changed since your last connection.</p>
            <p>This could indicate a man-in-the-middle attack, or the server may have simply updated its certificate.</p>
        `;
        elements.certOldFingerprintRow.style.display = 'flex';
        elements.certOldFingerprint.textContent = certInfo.oldFingerprint;
    } else {
        elements.certModalTitle.textContent = 'Certificate Verification Required';
        elements.certWarning.innerHTML = `
            <p>The server's certificate could not be verified. This may indicate:</p>
            <ul>
                <li>A self-signed certificate (common for internal servers)</li>
                <li>A certificate from an untrusted authority</li>
                <li>A potential security issue</li>
            </ul>
        `;
        elements.certOldFingerprintRow.style.display = 'none';
    }
    
    // Fill in the certificate details
    elements.certHost.textContent = `${certInfo.host}:${certInfo.port}`;
    elements.certCN.textContent = certInfo.commonName || '-';
    elements.certSubject.textContent = certInfo.subject || '-';
    elements.certIssuer.textContent = certInfo.issuer || '-';
    elements.certFingerprint.textContent = certInfo.fingerprint || '-';
    
    // Show the modal
    elements.certModal.classList.add('active');
}

function closeCertificateDialog(result) {
    elements.certModal.classList.remove('active');
    // Send response to C++ backend: 0=reject, 1=accept permanent, 2=accept temporary
    webui.call('certificateResponse', result);
}

// ============================================================================
// Authentication Dialog (called from C++ backend)
// ============================================================================
function showAuthDialog(authRequest) {
    console.log('[FRIDAY] Authentication requested:', authRequest);
    
    // Update title and prompt based on context
    if (authRequest.isGateway) {
        elements.authModalTitle.textContent = 'Gateway Authentication Required';
        elements.authPrompt.textContent = `Enter your credentials for gateway: ${authRequest.hostname}`;
    } else {
        elements.authModalTitle.textContent = 'Authentication Required';
        elements.authPrompt.textContent = `Enter your credentials for: ${authRequest.hostname}`;
    }
    
    // Pre-fill with existing values
    elements.authUsername.value = authRequest.currentUsername || '';
    elements.authDomain.value = authRequest.currentDomain || '';
    elements.authPassword.value = '';
    
    // Show the modal and focus password if username is filled
    elements.authModal.classList.add('active');
    if (elements.authUsername.value) {
        elements.authPassword.focus();
    } else {
        elements.authUsername.focus();
    }
}

function closeAuthDialog(success) {
    elements.authModal.classList.remove('active');
    
    if (success) {
        webui.call('authResponse', 
            true,
            elements.authUsername.value,
            elements.authPassword.value,
            elements.authDomain.value
        );
    } else {
        webui.call('authResponse', false, '', '', '');
    }
    
    // Clear password field for security
    elements.authPassword.value = '';
}

// ============================================================================
// AAD Authentication Dialog (called from C++ backend for Azure AD OAuth flow)
// ============================================================================
let aadAuthWindow = null;
let aadAuthCheckInterval = null;

function showAADAuthDialog(aadRequest) {
    console.log('[FRIDAY] AAD Authentication requested:', aadRequest);
    
    // Show a toast notification about the OAuth flow
    showToast(`Azure AD authentication required (${aadRequest.type}). Opening login window...`, 'info', 5000);
    
    // Open the auth URL in a popup window
    const width = 600;
    const height = 700;
    const left = (screen.width - width) / 2;
    const top = (screen.height - height) / 2;
    
    aadAuthWindow = window.open(
        aadRequest.authUrl,
        'AAD Login',
        `width=${width},height=${height},left=${left},top=${top},popup=yes,scrollbars=yes`
    );
    
    if (!aadAuthWindow) {
        // Popup was blocked
        console.error('[FRIDAY] AAD auth popup was blocked');
        showToast('Popup blocked! Please allow popups for AAD authentication.', 'error');
        webui.call('aadAuthResponse', false, '');
        return;
    }
    
    // Monitor the popup window for the redirect URL or closure
    aadAuthCheckInterval = setInterval(() => {
        try {
            // Check if window was closed by user
            if (aadAuthWindow.closed) {
                clearInterval(aadAuthCheckInterval);
                aadAuthCheckInterval = null;
                console.log('[FRIDAY] AAD auth window closed by user');
                showToast('Azure AD authentication cancelled', 'warning');
                webui.call('aadAuthResponse', false, '');
                return;
            }
            
            // Try to access the URL - will throw if cross-origin
            const currentUrl = aadAuthWindow.location.href;
            
            // Check if we got redirected back with an authorization code
            if (currentUrl.includes('code=') || currentUrl.includes('error=')) {
                clearInterval(aadAuthCheckInterval);
                aadAuthCheckInterval = null;
                aadAuthWindow.close();
                
                if (currentUrl.includes('code=')) {
                    console.log('[FRIDAY] AAD auth: received authorization code');
                    showToast('Azure AD authentication successful!', 'success');
                    webui.call('aadAuthResponse', true, currentUrl);
                } else {
                    // Error in OAuth flow
                    console.error('[FRIDAY] AAD auth error:', currentUrl);
                    showToast('Azure AD authentication failed', 'error');
                    webui.call('aadAuthResponse', false, '');
                }
                return;
            }
        } catch (e) {
            // Cross-origin error - window is still on the AAD domain, this is expected
            // Just continue polling
        }
    }, 500);
    
    // Set a maximum timeout (5 minutes)
    setTimeout(() => {
        if (aadAuthCheckInterval) {
            clearInterval(aadAuthCheckInterval);
            aadAuthCheckInterval = null;
            if (aadAuthWindow && !aadAuthWindow.closed) {
                aadAuthWindow.close();
            }
            console.log('[FRIDAY] AAD auth timed out');
            showToast('Azure AD authentication timed out', 'error');
            webui.call('aadAuthResponse', false, '');
        }
    }, 300000); // 5 minute timeout
}

function cancelAADAuth() {
    if (aadAuthCheckInterval) {
        clearInterval(aadAuthCheckInterval);
        aadAuthCheckInterval = null;
    }
    if (aadAuthWindow && !aadAuthWindow.closed) {
        aadAuthWindow.close();
    }
    webui.call('aadAuthResponse', false, '');
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
    
    // Fill advanced options
    setAdvancedOptions(conn);
    
    renderConnections();
}

// ============================================================================
// Advanced Options Management
// ============================================================================

function getAdvancedOptions() {
    return {
        home_drive: elements.optHomeDrive.checked,
        clipboard: elements.optClipboard.checked,
        cert_tofu: elements.optCertTofu.checked,
        usb_auto: elements.optUsbAuto.checked,
        floatbar: elements.optFloatbar.checked,
        dynamic_resolution: elements.optDynamicResolution.checked,
        network_auto: elements.optNetworkAuto.checked,
        gfx_avc420: elements.optGfxAvc420.checked,
        compression: elements.optCompression.checked,
        audio_pulse: elements.optAudioPulse.checked,
        prevent_session_lock: elements.optPreventLock.checked,
        auto_reconnect: elements.optAutoReconnect.checked,
        auto_reconnect_max_retries: parseInt(elements.optReconnectRetries.value) || 3
    };
}

function setAdvancedOptions(conn) {
    elements.optHomeDrive.checked = conn.home_drive || false;
    elements.optClipboard.checked = conn.clipboard !== false; // Default true
    elements.optCertTofu.checked = conn.cert_tofu || false;
    elements.optUsbAuto.checked = conn.usb_auto || false;
    elements.optFloatbar.checked = conn.floatbar || false;
    elements.optDynamicResolution.checked = conn.dynamic_resolution || false;
    elements.optNetworkAuto.checked = conn.network_auto || false;
    elements.optGfxAvc420.checked = conn.gfx_avc420 || false;
    elements.optCompression.checked = conn.compression || false;
    elements.optAudioPulse.checked = conn.audio_pulse || false;
    elements.optPreventLock.checked = conn.prevent_session_lock || false;
    elements.optAutoReconnect.checked = conn.auto_reconnect || false;
    elements.optReconnectRetries.value = conn.auto_reconnect_max_retries || 3;
    
    // Show/hide retries field
    updateReconnectRetriesVisibility();
}

function resetAdvancedOptions() {
    elements.optHomeDrive.checked = false;
    elements.optClipboard.checked = true;
    elements.optCertTofu.checked = false;
    elements.optUsbAuto.checked = false;
    elements.optFloatbar.checked = false;
    elements.optDynamicResolution.checked = false;
    elements.optNetworkAuto.checked = false;
    elements.optGfxAvc420.checked = false;
    elements.optCompression.checked = false;
    elements.optAudioPulse.checked = false;
    elements.optPreventLock.checked = false;
    elements.optAutoReconnect.checked = false;
    elements.optReconnectRetries.value = 3;
    updateReconnectRetriesVisibility();
}

function updateReconnectRetriesVisibility() {
    elements.reconnectRetriesGroup.style.display = 
        elements.optAutoReconnect.checked ? 'flex' : 'none';
}

function toggleAdvancedOptions() {
    elements.advancedToggle.classList.toggle('expanded');
    elements.advancedOptions.classList.toggle('visible');
}

// ============================================================================
// Backend Communication
// ============================================================================
async function loadConnections() {
    try {
        const result = await getConnections();
        connections = JSON.parse(result);
        renderConnections();
    } catch (error) {
        console.error('Failed to load connections:', error);
        showToast('Failed to load saved connections', 'error');
    }
}

async function loadAppInfo() {
    try {
        const result = await getAppInfo();
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
    
    // Get advanced options
    const options = getAdvancedOptions();
    
    if (!hostname) {
        showToast('Please enter a hostname or IP address', 'error');
        elements.hostname.focus();
        return;
    }
    
    updateStatus('Connecting...', false);
    elements.connectBtn.disabled = true;
    
    try {
        const result = await connectRDP(
            hostname, port, username, domain,
            options.home_drive, options.clipboard, options.cert_tofu,
            options.usb_auto, options.floatbar, options.dynamic_resolution,
            options.network_auto, options.gfx_avc420, options.compression,
            options.audio_pulse, options.prevent_session_lock,
            options.auto_reconnect, options.auto_reconnect_max_retries
        );
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
    
    // Get advanced options
    const options = getAdvancedOptions();
    
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
        const success = await saveConnection(
            name, hostname, port, username, domain,
            options.home_drive, options.clipboard, options.cert_tofu,
            options.usb_auto, options.floatbar, options.dynamic_resolution,
            options.network_auto, options.gfx_avc420, options.compression,
            options.audio_pulse, options.prevent_session_lock,
            options.auto_reconnect, options.auto_reconnect_max_retries
        );
        
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
        const success = await deleteConnection(name);
        
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
    
    // Advanced options toggle
    elements.advancedToggle.addEventListener('click', toggleAdvancedOptions);
    
    // Auto reconnect checkbox - show/hide retries field
    elements.optAutoReconnect.addEventListener('change', updateReconnectRetriesVisibility);
    
    // Save Modal controls
    elements.modalClose.addEventListener('click', closeSaveModal);
    elements.modalCancel.addEventListener('click', closeSaveModal);
    elements.modalSave.addEventListener('click', handleSaveConnection);
    
    // Save Modal backdrop click
    elements.saveModal.addEventListener('click', (e) => {
        if (e.target === elements.saveModal) {
            closeSaveModal();
        }
    });
    
    // Certificate Modal controls
    elements.certReject.addEventListener('click', () => closeCertificateDialog(0));
    elements.certAcceptPerm.addEventListener('click', () => closeCertificateDialog(1));
    elements.certAcceptTemp.addEventListener('click', () => closeCertificateDialog(2));
    
    // Certificate Modal backdrop click (reject on outside click)
    elements.certModal.addEventListener('click', (e) => {
        if (e.target === elements.certModal) {
            closeCertificateDialog(0);
        }
    });
    
    // Auth Modal controls
    elements.authModalClose.addEventListener('click', () => closeAuthDialog(false));
    elements.authCancel.addEventListener('click', () => closeAuthDialog(false));
    elements.authSubmit.addEventListener('click', () => closeAuthDialog(true));
    
    // Auth Modal backdrop click (cancel on outside click)
    elements.authModal.addEventListener('click', (e) => {
        if (e.target === elements.authModal) {
            closeAuthDialog(false);
        }
    });
    
    // Auth Modal - submit on Enter in password field
    elements.authPassword.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            e.preventDefault();
            closeAuthDialog(true);
        }
    });
    
    // Keyboard shortcuts
    document.addEventListener('keydown', (e) => {
        // Escape to close modals
        if (e.key === 'Escape') {
            if (elements.saveModal.classList.contains('active')) {
                closeSaveModal();
            }
            if (elements.certModal.classList.contains('active')) {
                closeCertificateDialog(0);  // Reject on escape
            }
            if (elements.authModal.classList.contains('active')) {
                closeAuthDialog(false);  // Cancel on escape
            }
        }
        
        // Enter in save modal to save
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
