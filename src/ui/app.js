/**
 * RDP Client - Frontend Application
 * 
 * Handles UI interactions and communicates with the C++ backend via WebUI bindings.
 */

// ============================================================================
// Global State
// ============================================================================
// Stores the last imported RDP file data for AVD-specific parameters
let importedRdpData = null;

// ============================================================================
// DOM Elements
// ============================================================================
const elements = {
    // Toolbar
    newConnectionBtn: document.getElementById('newConnectionBtn'),
    importBtn: document.getElementById('importBtn'),
    rdpFileInput: document.getElementById('rdpFileInput'),
    
    // Connections Grid
    connectionsGrid: document.getElementById('connectionsGrid'),
    
    // Connection Modal
    connectionModal: document.getElementById('connectionModal'),
    connectionModalTitle: document.getElementById('connectionModalTitle'),
    connectionModalClose: document.getElementById('connectionModalClose'),
    connectionModalCancel: document.getElementById('connectionModalCancel'),
    connectionModalSave: document.getElementById('connectionModalSave'),
    
    // Form fields
    connectionForm: document.getElementById('connectionForm'),
    connectionName: document.getElementById('connectionName'),
    hostname: document.getElementById('hostname'),
    port: document.getElementById('port'),
    username: document.getElementById('username'),
    domain: document.getElementById('domain'),
    
    // AVD Settings
    avdSettings: document.getElementById('avdSettings'),
    avdSettingsToggle: document.getElementById('avdSettingsToggle'),
    vmName: document.getElementById('vmName'),
    poolId: document.getElementById('poolId'),
    workspaceId: document.getElementById('workspaceId'),
    armPath: document.getElementById('armPath'),
    
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
    
    // Gateway / AVD Options
    optGatewayHostname: document.getElementById('optGatewayHostname'),
    optAadAuth: document.getElementById('optAadAuth'),
    optAadJoined: document.getElementById('optAadJoined'),
    optLoadBalanceInfo: document.getElementById('optLoadBalanceInfo'),
    loadBalanceInfoGroup: document.getElementById('loadBalanceInfoGroup'),
    
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
    
    // Delete Confirmation Modal
    deleteModal: document.getElementById('deleteModal'),
    deleteModalClose: document.getElementById('deleteModalClose'),
    deleteConnectionName: document.getElementById('deleteConnectionName'),
    
    // Context Menu
    contextMenu: document.getElementById('contextMenu'),
    contextMenuConnect: document.getElementById('contextMenuConnect'),
    contextMenuEdit: document.getElementById('contextMenuEdit'),
    contextMenuDelete: document.getElementById('contextMenuDelete'),
    
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
let contextMenuTarget = null; // Track which connection the context menu was opened for
let editingConnection = null; // Track which connection is being edited (null = new connection)

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
function openConnectionModal(connectionIndex = null) {
    editingConnection = connectionIndex;
    
    if (connectionIndex !== null) {
        // Editing existing connection
        const conn = connections[connectionIndex];
        elements.connectionModalTitle.textContent = 'Edit Connection';
        elements.connectionName.value = conn.name;
        elements.hostname.value = conn.hostname;
        elements.port.value = conn.port;
        elements.username.value = conn.username || '';
        elements.domain.value = conn.domain || '';
        setAdvancedOptions(conn);
    } else {
        // New connection
        elements.connectionModalTitle.textContent = 'New Connection';
        clearConnectionForm();
    }
    
    elements.connectionModal.classList.add('active');
    elements.connectionName.focus();
}

function closeConnectionModal() {
    elements.connectionModal.classList.remove('active');
    editingConnection = null;
    clearConnectionForm();
}

function clearConnectionForm() {
    elements.connectionName.value = '';
    elements.hostname.value = '';
    elements.port.value = '3389';
    elements.username.value = '';
    elements.domain.value = '';
    elements.vmName.value = '';
    elements.poolId.value = '';
    elements.workspaceId.value = '';
    elements.armPath.value = '';
    resetAdvancedOptions();
}

function openDeleteModal(connectionName) {
    elements.deleteConnectionName.textContent = connectionName;
    elements.deleteModal.classList.add('active');
}

function closeDeleteModal() {
    elements.deleteModal.classList.remove('active');
    contextMenuTarget = null;
}

// ============================================================================
// Context Menu Management
// ============================================================================
function showContextMenu(x, y, index) {
    contextMenuTarget = index;
    elements.contextMenu.style.left = x + 'px';
    elements.contextMenu.style.top = y + 'px';
    elements.contextMenu.classList.add('active');
    
    // Adjust position if menu goes off screen
    setTimeout(() => {
        const rect = elements.contextMenu.getBoundingClientRect();
        if (rect.right > window.innerWidth) {
            elements.contextMenu.style.left = (window.innerWidth - rect.width - 10) + 'px';
        }
        if (rect.bottom > window.innerHeight) {
            elements.contextMenu.style.top = (window.innerHeight - rect.height - 10) + 'px';
        }
    }, 0);
}

function hideContextMenu() {
    elements.contextMenu.classList.remove('active');
    contextMenuTarget = null;
}

function handleContextMenuConnect() {
    console.log('[RDPMAN] handleContextMenuConnect: contextMenuTarget=' + contextMenuTarget);
    if (contextMenuTarget !== null) {
        selectConnection(contextMenuTarget);
        handleConnect();
    }
    hideContextMenu();
}

function handleContextMenuEdit() {
    if (contextMenuTarget !== null) {
        openConnectionModal(contextMenuTarget);
    }
    hideContextMenu();
}

function handleContextMenuDelete() {
    if (contextMenuTarget !== null) {
        const conn = connections[contextMenuTarget];
        console.log('[RDPMAN] Delete requested for connection:', conn.name, 'at index:', contextMenuTarget);
        openDeleteModal(conn.name);
    }
    hideContextMenu();
}

// ============================================================================
// Certificate Dialog (called from C++ backend)
// ============================================================================
function showCertificateDialog(certInfo) {
    console.log('[RDPMAN] Certificate verification requested:', certInfo);
    
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
    console.log('[RDPMAN] Authentication requested:', authRequest);
    
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
// AAD Authentication Status (OAuth flow is now handled by native C++ WebUI window)
// ============================================================================

/**
 * Called from C++ backend to notify about AAD auth completion
 * The actual OAuth window is now a native WebUI window opened by the backend
 */
function onAADAuthComplete(success) {
    if (success) {
        console.log('[RDPMAN] AAD authentication successful');
        showToast('Azure AD authentication successful!', 'success');
    } else {
        console.log('[RDPMAN] AAD authentication failed or cancelled');
        showToast('Azure AD authentication failed or cancelled', 'warning');
    }
}

// Note: showAADAuthDialog is no longer used - the C++ backend now opens
// a native WebUI window for AAD authentication and intercepts the redirect
// URL with the authorization code automatically.

// ============================================================================
// Connections Grid (Icons View)
// ============================================================================
function renderConnections() {
    if (connections.length === 0) {
        elements.connectionsGrid.innerHTML = `
            <div class="empty-state">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <circle cx="12" cy="12" r="10"/>
                    <path d="M8 14s1.5 2 4 2 4-2 4-2"/>
                    <line x1="9" y1="9" x2="9.01" y2="9"/>
                    <line x1="15" y1="9" x2="15.01" y2="9"/>
                </svg>
                <p>No saved connections yet</p>
                <p class="empty-hint">Click "New Connection" to get started</p>
            </div>
        `;
        return;
    }
    
    elements.connectionsGrid.innerHTML = connections.map((conn, index) => `
        <div class="connection-icon-item ${selectedConnection === index ? 'selected' : ''}" 
             data-index="${index}">
            <div class="icon-wrapper">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <rect x="2" y="3" width="20" height="14" rx="2" ry="2"/>
                    <line x1="8" y1="21" x2="16" y2="21"/>
                    <line x1="12" y1="17" x2="12" y2="21"/>
                </svg>
            </div>
            <div class="connection-name">${escapeHtml(conn.name)}</div>
            <div class="connection-host">${escapeHtml(conn.hostname)}:${conn.port}</div>
        </div>
    `).join('');
}

function selectConnection(index) {
    selectedConnection = index;
    const conn = connections[index];
    
    // Clear imported RDP data when loading a saved connection
    importedRdpData = null;
    
    // Fill form with connection details
    elements.hostname.value = conn.hostname;
    elements.port.value = conn.port;
    elements.username.value = conn.username || '';
    elements.domain.value = conn.domain || '';
    
    // Fill advanced options
    setAdvancedOptions(conn);
    
    // Update selection visually without re-rendering (preserves DOM for dblclick)
    elements.connectionsGrid.querySelectorAll('.connection-icon-item').forEach(item => {
        const i = parseInt(item.dataset.index);
        item.classList.toggle('selected', i === index);
    });
}

// ============================================================================
// Advanced Options Management
// ============================================================================

function getAdvancedOptions() {
    return {
        home_drive: elements.optHomeDrive?.checked || false,
        clipboard: elements.optClipboard?.checked !== false,  // Default true
        cert_tofu: elements.optCertTofu?.checked || false,
        usb_auto: elements.optUsbAuto?.checked || false,
        floatbar: elements.optFloatbar?.checked || false,
        dynamic_resolution: elements.optDynamicResolution?.checked || false,
        network_auto: elements.optNetworkAuto?.checked || false,
        gfx_avc420: elements.optGfxAvc420?.checked || false,
        compression: elements.optCompression?.checked || false,
        audio_pulse: elements.optAudioPulse?.checked || false,
        prevent_session_lock: elements.optPreventLock?.checked || false,
        auto_reconnect: elements.optAutoReconnect?.checked || false,
        auto_reconnect_max_retries: parseInt(elements.optReconnectRetries?.value) || 3,
        // Gateway / AVD options
        gateway_hostname: elements.optGatewayHostname?.value?.trim() || '',
        enable_rds_aad_auth: elements.optAadAuth?.checked || false,
        target_is_aad_joined: elements.optAadJoined?.checked || false,
        load_balance_info: elements.optLoadBalanceInfo?.value?.trim() || '',
        // AVD-specific fields (now editable)
        aad_tenant_id: importedRdpData?.aad_tenant_id || '',
        wvd_endpoint_pool: elements.poolId?.value?.trim() || '',
        arm_path: elements.armPath?.value?.trim() || '',
        workspace_id: elements.workspaceId?.value?.trim() || '',
        remote_application_program: importedRdpData?.remote_application_program || '',
        remote_desktop_name: elements.vmName?.value?.trim() || ''
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
    
    // Gateway / AVD options
    elements.optGatewayHostname.value = conn.gateway_hostname || '';
    elements.optAadAuth.checked = conn.enable_rds_aad_auth || false;
    elements.optAadJoined.checked = conn.target_is_aad_joined || false;
    elements.optLoadBalanceInfo.value = conn.load_balance_info || '';
    
    // AVD-specific settings
    elements.vmName.value = conn.remote_desktop_name || '';
    elements.poolId.value = conn.wvd_endpoint_pool || '';
    elements.workspaceId.value = conn.workspace_id || '';
    elements.armPath.value = conn.arm_path || '';
    
    // Show/hide conditional fields
    updateReconnectRetriesVisibility();
    updateAvdFieldsVisibility();
}

function resetAdvancedOptions() {
    // Clear imported RDP data
    importedRdpData = null;
    
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
    // Gateway / AVD options
    elements.optGatewayHostname.value = '';
    elements.optAadAuth.checked = false;
    elements.optAadJoined.checked = false;
    elements.optLoadBalanceInfo.value = '';
    updateReconnectRetriesVisibility();
    updateAvdFieldsVisibility();
}

function updateAvdFieldsVisibility() {
    // Show load balance info field when AAD options are enabled
    const showAvdFields = elements.optAadAuth.checked || elements.optAadJoined.checked;
    elements.loadBalanceInfoGroup.style.display = showAvdFields ? 'block' : 'none';
}

function updateReconnectRetriesVisibility() {
    elements.reconnectRetriesGroup.style.display = 
        elements.optAutoReconnect.checked ? 'flex' : 'none';
}

function populateAvdSettings(rdpData) {
    console.log('[RDPMAN] Populating AVD settings:', rdpData);
    
    if (rdpData.remote_desktop_name) {
        elements.vmName.value = rdpData.remote_desktop_name;
    }
    if (rdpData.wvd_endpoint_pool) {
        elements.poolId.value = rdpData.wvd_endpoint_pool;
    }
    if (rdpData.workspace_id) {
        elements.workspaceId.value = rdpData.workspace_id;
    }
    if (rdpData.arm_path) {
        elements.armPath.value = rdpData.arm_path;
    }
}

function toggleAdvancedOptions() {
    elements.advancedToggle.classList.toggle('expanded');
    elements.advancedOptions.classList.toggle('visible');
}

function toggleAvdSettings() {
    elements.avdSettingsToggle.classList.toggle('expanded');
    elements.avdSettings.classList.toggle('visible');
}

// ============================================================================
// RDP File Import
// ============================================================================

async function handleImportRdpFile(file) {
    if (!file) return;
    
    console.log('[RDPMAN] Importing RDP file:', file.name);
    
    // Read the file content
    const content = await file.text();
    
    try {
        const result = await importRdpFile(content);
        const data = JSON.parse(result);
        
        if (!data.success) {
            showToast(`Failed to parse RDP file: ${data.error}`, 'error');
            return;
        }
        
        const rdp = data.data;
        console.log('[RDPMAN] Parsed RDP file:', rdp);
        
        // Store the full RDP data for later use (includes AVD-specific fields)
        importedRdpData = rdp;
        
        // Open the connection modal with imported data
        openConnectionModal();
        
        // Fill in the form with parsed values
        if (rdp.display_name || rdp.full_address) {
            elements.connectionName.value = rdp.display_name || rdp.full_address;
        }
        if (elements.hostname) elements.hostname.value = rdp.full_address || '';
        if (elements.port) elements.port.value = rdp.server_port || 3389;
        if (elements.username) elements.username.value = rdp.username || '';
        if (elements.domain) elements.domain.value = rdp.domain || '';
        
        // Set advanced options
        if (elements.optDynamicResolution) elements.optDynamicResolution.checked = rdp.dynamic_resolution || false;
        if (elements.optClipboard) elements.optClipboard.checked = rdp.redirect_clipboard !== false;
        
        // Gateway settings
        if (rdp.gateway_hostname && elements.optGatewayHostname) {
            elements.optGatewayHostname.value = rdp.gateway_hostname;
        }
        
        // AVD / AAD settings
        if (elements.optAadAuth) elements.optAadAuth.checked = rdp.enable_rds_aad_auth || false;
        if (elements.optAadJoined) elements.optAadJoined.checked = rdp.target_is_aad_joined || false;
        
        if (rdp.load_balance_info && elements.optLoadBalanceInfo) {
            elements.optLoadBalanceInfo.value = rdp.load_balance_info;
        }
        
        // Show the advanced options if we have gateway/AVD settings
        if (rdp.is_avd_connection || rdp.uses_gateway) {
            if (elements.advancedOptions && !elements.advancedOptions.classList.contains('visible')) {
                toggleAdvancedOptions();
            }
        }
        
        updateAvdFieldsVisibility();
        
        // Populate AVD settings if this is an AVD connection
        if (rdp.is_avd_connection) {
            populateAvdSettings(rdp);
        }
        
        // Show success message
        const displayName = rdp.display_name || rdp.full_address;
        showToast(`Imported: ${displayName}`, 'success');
        
        if (rdp.is_avd_connection) {
            showToast('AVD/Dev Box connection detected - AAD authentication will be used', 'info', 5000);
        }
        
    } catch (error) {
        console.error('[RDPMAN] Import error:', error);
        showToast('Failed to import RDP file', 'error');
    }
}

// ============================================================================
// Backend Communication
// ============================================================================
async function loadConnections() {
    try {
        const result = await getConnections();
        if (result === undefined || result === null || result === '') {
            console.warn('[RDPMAN] getConnections returned empty result');
            connections = [];
        } else {
            connections = JSON.parse(result);
        }
        console.log('[RDPMAN] Loaded', connections.length, 'connections');
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
    console.log('[RDPMAN] handleConnect() called');
    const hostname = elements.hostname.value.trim();
    const port = parseInt(elements.port.value) || 3389;
    const username = elements.username.value.trim();
    const domain = elements.domain.value.trim();
    
    console.log('[RDPMAN] handleConnect: hostname=' + hostname + ' port=' + port);
    
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
        // Build connection params as JSON object
        const connectionParams = {
            hostname: hostname,
            port: port,
            username: username,
            domain: domain,
            // Advanced options
            home_drive: options.home_drive,
            clipboard: options.clipboard,
            cert_tofu: options.cert_tofu,
            usb_auto: options.usb_auto,
            floatbar: options.floatbar,
            dynamic_resolution: options.dynamic_resolution,
            network_auto: options.network_auto,
            gfx_avc420: options.gfx_avc420,
            compression: options.compression,
            audio_pulse: options.audio_pulse,
            prevent_session_lock: options.prevent_session_lock,
            auto_reconnect: options.auto_reconnect,
            auto_reconnect_max_retries: options.auto_reconnect_max_retries,
            // Gateway / AVD options
            gateway_hostname: options.gateway_hostname,
            enable_rds_aad_auth: options.enable_rds_aad_auth,
            target_is_aad_joined: options.target_is_aad_joined,
            load_balance_info: options.load_balance_info,
            // Additional AVD parameters
            aad_tenant_id: options.aad_tenant_id,
            wvd_endpoint_pool: options.wvd_endpoint_pool,
            arm_path: options.arm_path,
            workspace_id: options.workspace_id,
            remote_application_program: options.remote_application_program
        };
        
        console.log('[RDPMAN] Calling connectRDP with params:', JSON.stringify(connectionParams).substring(0, 200));
        const result = await connectRDP(JSON.stringify(connectionParams));
        console.log('[RDPMAN] connectRDP result:', result);
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
        let errorMsg = 'Unknown error';
        if (error instanceof Error) {
            errorMsg = error.message;
        } else if (typeof error === 'string') {
            errorMsg = error;
        } else if (error) {
            errorMsg = JSON.stringify(error);
        }
        showToast('Failed to initiate connection: ' + errorMsg, 'error');
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
        showToast('Please enter a hostname', 'error');
        elements.hostname.focus();
        return;
    }
    
    try {
        // Build connection params as JSON object (WebUI has 16-arg limit)
        const connectionParams = {
            name: name,
            hostname: hostname,
            port: port,
            username: username,
            domain: domain,
            // Advanced options
            home_drive: options.home_drive,
            clipboard: options.clipboard,
            cert_tofu: options.cert_tofu,
            usb_auto: options.usb_auto,
            floatbar: options.floatbar,
            dynamic_resolution: options.dynamic_resolution,
            network_auto: options.network_auto,
            gfx_avc420: options.gfx_avc420,
            compression: options.compression,
            audio_pulse: options.audio_pulse,
            prevent_session_lock: options.prevent_session_lock,
            auto_reconnect: options.auto_reconnect,
            auto_reconnect_max_retries: options.auto_reconnect_max_retries,
            // AVD/Dev Box fields
            remote_desktop_name: options.remote_desktop_name,
            wvd_endpoint_pool: options.wvd_endpoint_pool,
            workspace_id: options.workspace_id,
            arm_path: options.arm_path
        };
        
        const success = await saveConnection(JSON.stringify(connectionParams));
        
        if (success) {
            showToast(`Connection "${name}" saved`, 'success');
            closeConnectionModal();
            await loadConnections();
            
            // Auto-connect after saving
            const connIndex = connections.findIndex(c => c.name === name);
            if (connIndex !== -1) {
                selectConnection(connIndex);
                handleConnect();
            }
        } else {
            showToast('Failed to save connection', 'error');
        }
    } catch (error) {
        console.error('Save error:', error);
        showToast('Failed to save connection', 'error');
    }
}

async function handleDeleteConnection(name) {
    console.log('[RDPMAN] handleDeleteConnection called for:', name);
    try {
        console.log('[RDPMAN] Calling deleteConnection backend function...');
        const success = await deleteConnection(name);
        console.log('[RDPMAN] deleteConnection returned:', success);
        
        if (success) {
            showToast(`Connection "${name}" deleted`, 'info');
            selectedConnection = null;
            closeDeleteModal();
            await loadConnections();
        } else {
            showToast('Failed to delete connection', 'error');
        }
    } catch (error) {
        console.error('[RDPMAN] Delete error:', error);
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
    // Toolbar buttons
    elements.newConnectionBtn.addEventListener('click', () => {
        openConnectionModal();
    });
    
    // Import button and file input
    elements.importBtn.addEventListener('click', () => {
        elements.rdpFileInput.click();
    });
    
    elements.rdpFileInput.addEventListener('change', (e) => {
        if (e.target.files.length > 0) {
            handleImportRdpFile(e.target.files[0]);
            e.target.value = ''; // Reset for next import
        }
    });
    
    // Connection Modal controls
    elements.connectionModalClose.addEventListener('click', closeConnectionModal);
    elements.connectionModalCancel.addEventListener('click', closeConnectionModal);
    elements.connectionModalSave.addEventListener('click', handleSaveConnection);
    
    // Connection Modal backdrop click
    elements.connectionModal.addEventListener('click', (e) => {
        if (e.target === elements.connectionModal) {
            closeConnectionModal();
        }
    });
    
    // Advanced options toggle
    elements.advancedToggle.addEventListener('click', toggleAdvancedOptions);
    
    // AVD settings toggle
    elements.avdSettingsToggle.addEventListener('click', toggleAvdSettings);
    
    // Auto reconnect checkbox - show/hide retries field
    elements.optAutoReconnect.addEventListener('change', updateReconnectRetriesVisibility);
    
    // AAD checkboxes - show/hide AVD fields
    elements.optAadAuth.addEventListener('change', updateAvdFieldsVisibility);
    elements.optAadJoined.addEventListener('change', updateAvdFieldsVisibility);
    
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
    
    // Delete Modal controls - use event delegation to avoid WebUI interception
    elements.deleteModalClose.addEventListener('click', closeDeleteModal);
    
    // Handle delete modal actions via event delegation on the footer
    console.log('[RDPMAN] Setting up delete modal, deleteModal:', elements.deleteModal);
    const deleteModalFooter = elements.deleteModal?.querySelector('.modal-footer');
    console.log('[RDPMAN] Delete modal footer found:', deleteModalFooter);
    
    if (deleteModalFooter) {
        console.log('[RDPMAN] Adding click listener to delete modal footer');
        deleteModalFooter.addEventListener('click', (e) => {
            const action = e.target.closest('[data-action]')?.dataset.action;
            console.log('[RDPMAN] Delete modal action clicked:', action, 'target:', e.target);
            
            if (action === 'cancel-delete') {
                closeDeleteModal();
            } else if (action === 'confirm-delete') {
                console.log('[RDPMAN] Delete confirm button clicked, contextMenuTarget:', contextMenuTarget);
                if (contextMenuTarget !== null) {
                    const conn = connections[contextMenuTarget];
                    console.log('[RDPMAN] Deleting connection:', conn);
                    handleDeleteConnection(conn.name);
                } else {
                    console.error('[RDPMAN] contextMenuTarget is null!');
                }
            }
        });
    } else {
        console.error('[RDPMAN] Delete modal footer not found!');
    }
    
    // Delete Modal backdrop click
    elements.deleteModal.addEventListener('click', (e) => {
        if (e.target === elements.deleteModal) {
            closeDeleteModal();
        }
    });
    
    // Context Menu controls
    elements.contextMenuConnect.addEventListener('click', handleContextMenuConnect);
    elements.contextMenuEdit.addEventListener('click', handleContextMenuEdit);
    elements.contextMenuDelete.addEventListener('click', handleContextMenuDelete);
    
    // Connection grid event delegation (survives innerHTML re-renders)
    elements.connectionsGrid.addEventListener('click', (e) => {
        const item = e.target.closest('.connection-icon-item');
        if (item) {
            const index = parseInt(item.dataset.index);
            console.log('[RDPMAN] Grid click: selecting connection ' + index);
            selectConnection(index);
        }
    });
    elements.connectionsGrid.addEventListener('dblclick', (e) => {
        const item = e.target.closest('.connection-icon-item');
        if (item) {
            const index = parseInt(item.dataset.index);
            console.log('[RDPMAN] Grid dblclick: connecting to ' + index);
            selectConnection(index);
            handleConnect();
        }
    });
    elements.connectionsGrid.addEventListener('contextmenu', (e) => {
        const item = e.target.closest('.connection-icon-item');
        if (item) {
            e.preventDefault();
            const index = parseInt(item.dataset.index);
            selectConnection(index);
            showContextMenu(e.clientX, e.clientY, index);
        }
    });
    
    // Hide context menu when clicking outside
    document.addEventListener('click', (e) => {
        if (!e.target.closest('.context-menu')) {
            hideContextMenu();
        }
    });
    
    // Keyboard shortcuts
    document.addEventListener('keydown', (e) => {
        // Escape to close modals
        if (e.key === 'Escape') {
            if (elements.connectionModal.classList.contains('active')) {
                closeConnectionModal();
            }
            if (elements.certModal.classList.contains('active')) {
                closeCertificateDialog(0);  // Reject on escape
            }
            if (elements.authModal.classList.contains('active')) {
                closeAuthDialog(false);  // Cancel on escape
            }
            if (elements.deleteModal.classList.contains('active')) {
                closeDeleteModal();
            }
            hideContextMenu();
        }
        
        // Ctrl+N to open new connection
        if (e.ctrlKey && e.key === 'n') {
            e.preventDefault();
            openConnectionModal();
        }
    });
}

// ============================================================================
// Initialization
// ============================================================================

// Called by C++ when WebUI connection is established
async function onWebuiReady() {
    console.log('[RDPMAN] WebUI connection ready, loading data...');
    await loadAppInfo();
    await loadConnections();
    console.log('[RDPMAN] Initial data loaded.');
}

async function init() {
    console.log('[RDPMAN] Initializing interface...');
    
    initEventListeners();
    
    console.log('[RDPMAN] Interface ready, waiting for WebUI connection...');
}

// Start when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
