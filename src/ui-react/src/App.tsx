import { useState, useCallback, useRef, useEffect } from 'react';
import {
  makeStyles,
  tokens,
  Toaster,
  useToastController,
  useId,
  Toast,
  ToastTitle,
  ToastBody,
} from '@fluentui/react-components';
import type {
  ConnectionProfile,
  ConnectionParams,
  CertificateInfo,
  AuthRequest,
  AppInfo,
} from './types';
import { defaultConnectionProfile } from './types';
import {
  apiGetConnections,
  apiGetAppInfo,
  apiConnect,
  apiSaveConnection,
  apiDeleteConnection,
  apiImportRdpFile,
  apiCertificateResponse,
  apiAuthResponse,
} from './api';
import { AppHeader } from './components/AppHeader';
import { Toolbar } from './components/Toolbar';
import { ConnectionGrid } from './components/ConnectionGrid';
import { ConnectionEditorDialog } from './components/ConnectionEditorDialog';
import { CertificateDialog } from './components/CertificateDialog';
import { AuthDialog } from './components/AuthDialog';
import { DeleteDialog } from './components/DeleteDialog';

const useStyles = makeStyles({
  root: {
    display: 'flex',
    flexDirection: 'column',
    height: '100vh',
    maxWidth: '1400px',
    margin: '0 auto',
    padding: tokens.spacingHorizontalL,
    gap: tokens.spacingVerticalM,
    boxSizing: 'border-box',
  },
  main: {
    flex: 1,
    minHeight: 0,
    overflow: 'auto',
  },
  footer: {
    padding: tokens.spacingVerticalS,
    textAlign: 'center' as const,
    color: tokens.colorNeutralForeground4,
    fontSize: tokens.fontSizeBase200,
  },
});

export function App() {
  const styles = useStyles();
  const toasterId = useId('toaster');
  const { dispatchToast } = useToastController(toasterId);

  // State
  const [connections, setConnections] = useState<ConnectionProfile[]>([]);
  const [selectedIndex, setSelectedIndex] = useState<number | null>(null);
  const [statusText, setStatusText] = useState('Systems Online');
  const [statusOnline, setStatusOnline] = useState(true);
  const [appInfo, setAppInfo] = useState<AppInfo | null>(null);
  const [importedRdpData, setImportedRdpData] = useState<Record<string, unknown> | null>(null);

  // Dialog state
  const [editorOpen, setEditorOpen] = useState(false);
  const [editorProfile, setEditorProfile] = useState<ConnectionProfile>(defaultConnectionProfile());
  const [editorIsNew, setEditorIsNew] = useState(true);

  const [certDialogOpen, setCertDialogOpen] = useState(false);
  const [certInfo, setCertInfo] = useState<CertificateInfo | null>(null);

  const [authDialogOpen, setAuthDialogOpen] = useState(false);
  const [authRequest, setAuthRequest] = useState<AuthRequest | null>(null);

  const [deleteDialogOpen, setDeleteDialogOpen] = useState(false);
  const [deleteTargetName, setDeleteTargetName] = useState('');

  // Ref to track imported RDP data for connection params
  const importedRdpDataRef = useRef<Record<string, unknown> | null>(null);
  importedRdpDataRef.current = importedRdpData;

  // Toast helper
  const showToast = useCallback(
    (message: string, intent: 'success' | 'error' | 'info' | 'warning' = 'info') => {
      dispatchToast(
        <Toast>
          <ToastTitle>{message}</ToastTitle>
        </Toast>,
        { intent, timeout: 4000, position: 'bottom-end' }
      );
    },
    [dispatchToast]
  );

  // Load connections
  const loadConnections = useCallback(async () => {
    const conns = await apiGetConnections();
    setConnections(conns);
  }, []);

  // Load app info
  const loadAppInfo = useCallback(async () => {
    const info = await apiGetAppInfo();
    setAppInfo(info);
  }, []);

  // Connect
  const handleConnect = useCallback(
    async (conn: ConnectionProfile) => {
      if (!conn.hostname) {
        showToast('Please enter a hostname or IP address', 'error');
        return;
      }
      setStatusText('Connecting...');
      setStatusOnline(false);

      const params: ConnectionParams = {
        ...conn,
        aad_tenant_id: (importedRdpDataRef.current?.aad_tenant_id as string) || conn.aad_tenant_id || '',
        remote_application_program:
          (importedRdpDataRef.current?.remote_application_program as string) || conn.remote_application_program || '',
      };

      const result = await apiConnect(params);
      if (result.success) {
        showToast(`Connecting to ${conn.hostname}...`, 'success');
        setStatusText('Session Active');
        setStatusOnline(true);
      } else {
        showToast(`Connection failed: ${result.error}`, 'error');
        setStatusText('Connection Failed');
        setStatusOnline(false);
        setTimeout(() => {
          setStatusText('Systems Online');
          setStatusOnline(true);
        }, 3000);
      }
    },
    [showToast]
  );

  // Save & connect
  const handleSaveConnection = useCallback(
    async (profile: ConnectionProfile) => {
      if (!profile.name) {
        showToast('Please enter a connection name', 'error');
        return;
      }
      if (!profile.hostname) {
        showToast('Please enter a hostname', 'error');
        return;
      }
      const success = await apiSaveConnection(profile);
      if (success) {
        showToast(`Connection "${profile.name}" saved`, 'success');
        setEditorOpen(false);
        await loadConnections();
        // Auto-connect
        handleConnect(profile);
      } else {
        showToast('Failed to save connection', 'error');
      }
    },
    [showToast, loadConnections, handleConnect]
  );

  // Delete
  const handleDeleteConnection = useCallback(
    async (name: string) => {
      const success = await apiDeleteConnection(name);
      if (success) {
        showToast(`Connection "${name}" deleted`, 'info');
        setSelectedIndex(null);
        setDeleteDialogOpen(false);
        await loadConnections();
      } else {
        showToast('Failed to delete connection', 'error');
      }
    },
    [showToast, loadConnections]
  );

  // Import RDP file
  const handleImportFile = useCallback(
    async (file: File) => {
      const content = await file.text();
      const result = await apiImportRdpFile(content);
      if (!result.success || !result.data) {
        showToast(`Failed to parse RDP file: ${result.error}`, 'error');
        return;
      }
      const rdp = result.data;
      setImportedRdpData(rdp as unknown as Record<string, unknown>);

      const profile: ConnectionProfile = {
        ...defaultConnectionProfile(),
        name: rdp.display_name || rdp.full_address || '',
        hostname: rdp.full_address || '',
        port: rdp.server_port || 3389,
        username: rdp.username || '',
        domain: rdp.domain || '',
        dynamic_resolution: rdp.dynamic_resolution || false,
        clipboard: rdp.redirect_clipboard !== false,
        gateway_hostname: rdp.gateway_hostname || '',
        enable_rds_aad_auth: rdp.enable_rds_aad_auth || false,
        target_is_aad_joined: rdp.target_is_aad_joined || false,
        load_balance_info: rdp.load_balance_info || '',
        remote_desktop_name: rdp.remote_desktop_name || '',
        wvd_endpoint_pool: rdp.wvd_endpoint_pool || '',
        workspace_id: rdp.workspace_id || '',
        arm_path: rdp.arm_path || '',
        aad_tenant_id: rdp.aad_tenant_id || '',
        remote_application_program: rdp.remote_application_program || '',
      };
      setEditorProfile(profile);
      setEditorIsNew(true);
      setEditorOpen(true);

      const displayName = rdp.display_name || rdp.full_address;
      showToast(`Imported: ${displayName}`, 'success');
      if (rdp.is_avd_connection) {
        showToast('AVD/Dev Box connection detected – AAD authentication will be used', 'info');
      }
    },
    [showToast]
  );

  // Grid actions
  const handleNewConnection = useCallback(() => {
    setEditorProfile(defaultConnectionProfile());
    setEditorIsNew(true);
    setEditorOpen(true);
    setImportedRdpData(null);
  }, []);

  const handleEditConnection = useCallback(
    (index: number) => {
      setEditorProfile({ ...connections[index] });
      setEditorIsNew(false);
      setEditorOpen(true);
    },
    [connections]
  );

  const handleRequestDelete = useCallback(
    (index: number) => {
      setDeleteTargetName(connections[index].name);
      setDeleteDialogOpen(true);
    },
    [connections]
  );

  const handleDoubleClick = useCallback(
    (index: number) => {
      setSelectedIndex(index);
      handleConnect(connections[index]);
    },
    [connections, handleConnect]
  );

  const handleContextConnect = useCallback(
    (index: number) => {
      setSelectedIndex(index);
      handleConnect(connections[index]);
    },
    [connections, handleConnect]
  );

  // Certificate dialog handlers
  const handleCertReject = useCallback(() => {
    setCertDialogOpen(false);
    apiCertificateResponse(0);
  }, []);
  const handleCertAcceptTemp = useCallback(() => {
    setCertDialogOpen(false);
    apiCertificateResponse(2);
  }, []);
  const handleCertAcceptPerm = useCallback(() => {
    setCertDialogOpen(false);
    apiCertificateResponse(1);
  }, []);

  // Auth dialog handlers
  const handleAuthSubmit = useCallback(
    (username: string, password: string, domain: string) => {
      setAuthDialogOpen(false);
      apiAuthResponse(true, username, password, domain);
    },
    []
  );
  const handleAuthCancel = useCallback(() => {
    setAuthDialogOpen(false);
    apiAuthResponse(false, '', '', '');
  }, []);

  // Register global callbacks for C++ backend
  useEffect(() => {
    (window as unknown as Record<string, unknown>).showCertificateDialog = (
      info: CertificateInfo
    ) => {
      setCertInfo(info);
      setCertDialogOpen(true);
    };

    (window as unknown as Record<string, unknown>).showAuthDialog = (req: AuthRequest) => {
      setAuthRequest(req);
      setAuthDialogOpen(true);
    };

    (window as unknown as Record<string, unknown>).onAADAuthComplete = (success: boolean) => {
      if (success) {
        dispatchToast(
          <Toast>
            <ToastTitle>Azure AD authentication successful!</ToastTitle>
          </Toast>,
          { intent: 'success', timeout: 4000, position: 'bottom-end' }
        );
      } else {
        dispatchToast(
          <Toast>
            <ToastTitle>Azure AD authentication failed or cancelled</ToastTitle>
          </Toast>,
          { intent: 'warning', timeout: 4000, position: 'bottom-end' }
        );
      }
    };

    (window as unknown as Record<string, unknown>).onWebuiReady = async () => {
      console.log('[RDPMAN] WebUI connection ready, loading data...');
      await loadAppInfo();
      await loadConnections();
      console.log('[RDPMAN] Initial data loaded.');
    };
  }, [dispatchToast, loadAppInfo, loadConnections]);

  // Keyboard shortcut: Ctrl+N for new connection
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (e.ctrlKey && e.key === 'n') {
        e.preventDefault();
        handleNewConnection();
      }
    };
    document.addEventListener('keydown', handler);
    return () => document.removeEventListener('keydown', handler);
  }, [handleNewConnection]);

  const footerText = appInfo
    ? `${appInfo.name} v${appInfo.version} | FreeRDP ${appInfo.freerdp_version}`
    : 'WebUI RDP Client v0.1.0';

  return (
    <div className={styles.root}>
      <AppHeader statusText={statusText} statusOnline={statusOnline} />
      <Toolbar onNewConnection={handleNewConnection} onImportFile={handleImportFile} />
      <div className={styles.main}>
        <ConnectionGrid
          connections={connections}
          selectedIndex={selectedIndex}
          onSelect={setSelectedIndex}
          onDoubleClick={handleDoubleClick}
          onConnect={handleContextConnect}
          onEdit={handleEditConnection}
          onDelete={handleRequestDelete}
        />
      </div>
      <div className={styles.footer}>{footerText}</div>

      <Toaster toasterId={toasterId} />

      <ConnectionEditorDialog
        open={editorOpen}
        profile={editorProfile}
        isNew={editorIsNew}
        onSave={handleSaveConnection}
        onCancel={() => setEditorOpen(false)}
      />

      <CertificateDialog
        open={certDialogOpen}
        certInfo={certInfo}
        onReject={handleCertReject}
        onAcceptTemp={handleCertAcceptTemp}
        onAcceptPerm={handleCertAcceptPerm}
      />

      <AuthDialog
        open={authDialogOpen}
        authRequest={authRequest}
        onSubmit={handleAuthSubmit}
        onCancel={handleAuthCancel}
      />

      <DeleteDialog
        open={deleteDialogOpen}
        connectionName={deleteTargetName}
        onConfirm={() => handleDeleteConnection(deleteTargetName)}
        onCancel={() => setDeleteDialogOpen(false)}
      />
    </div>
  );
}
