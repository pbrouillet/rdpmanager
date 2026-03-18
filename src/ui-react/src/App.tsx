import { useState, useCallback, useRef, useEffect } from 'react';
import {
  FluentProvider,
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
  DatabaseStatus,
  FeedAccount,
  ViewMode,
} from './types';
import { defaultConnectionProfile, INHERITABLE_FIELDS } from './types';
import {
  apiGetConnections,
  apiGetAppInfo,
  apiConnect,
  apiSaveConnection,
  apiDeleteConnection,
  apiImportRdpFile,
  apiCreateDatabase,
  apiCreateDatabaseDialog,
  apiOpenDatabase,
  apiOpenDatabaseDialog,
  apiCloneDatabase,
  apiCloseDatabase,
  apiGetDatabaseStatus,
  apiCreateFolder,
  apiMoveFolder,
  apiRenameFolder,
  apiDeleteFolder,
  apiGetFolders,
  apiCertificateResponse,
  apiAuthResponse,
  apiGetFeedAccounts,
  apiDeleteFeedAccount,
  apiDiscoverFeeds,
  apiLogOffAccount,
  apiForgetAccount,
  apiSaveFolderSettings,
  apiGetEffectiveConnectionProfile,
  apiAadAuthResponse,
} from './api';
import { AppHeader } from './components/AppHeader';
import { DatabaseTabs } from './components/DatabaseTabs';
import { Toolbar } from './components/Toolbar';
import { ConnectionGrid } from './components/ConnectionGrid';
import { ConnectionTable } from './components/ConnectionTable';
import { FolderTree } from './components/FolderTree';
import { ConnectionEditorDialog } from './components/ConnectionEditorDialog';
import { CertificateDialog } from './components/CertificateDialog';
import { AuthDialog } from './components/AuthDialog';
import { DeleteDialog } from './components/DeleteDialog';
import { AccountActionDialog, type AccountAction } from './components/AccountActionDialog';
import { FolderSettingsDialog } from './components/FolderSettingsDialog';
import { ManualCodeFlowDialog, type ManualCodeFlowInfo } from './components/ManualCodeFlowDialog';
import { getFluentTheme, type ThemeMode } from './theme';

const useStyles = makeStyles({
  root: {
    display: 'flex',
    flexDirection: 'column',
    height: '100vh',
    width: '100%',
    margin: 0,
    padding: 0,
    gap: 0,
    boxSizing: 'border-box',
    backgroundColor: tokens.colorNeutralBackground2,
  },
  main: {
    flex: 1,
    minHeight: 0,
    overflow: 'hidden',
    display: 'flex',
    gap: 0,
  },
  leftPane: {
    minWidth: '180px',
    maxWidth: '520px',
    height: '100%',
    overflow: 'hidden',
    borderRight: `1px solid ${tokens.colorNeutralStroke2}`,
  },
  splitter: {
    width: '4px',
    cursor: 'col-resize',
    backgroundColor: tokens.colorNeutralBackground2,
    ':hover': {
      backgroundColor: tokens.colorNeutralStroke2,
    },
  },
  contentPane: {
    flex: 1,
    minWidth: 0,
    height: '100%',
    overflow: 'auto',
  },
  footer: {
    padding: `${tokens.spacingVerticalXS} ${tokens.spacingHorizontalM}`,
    textAlign: 'center' as const,
    color: tokens.colorNeutralForeground4,
    fontSize: tokens.fontSizeBase200,
    borderTop: `1px solid ${tokens.colorNeutralStroke2}`,
    backgroundColor: tokens.colorNeutralBackground1,
  },
});

export function App() {
  const styles = useStyles();
  const toasterId = useId('toaster');
  const { dispatchToast } = useToastController(toasterId);
  const [themeMode, setThemeMode] = useState<ThemeMode>(() => {
    const saved = window.localStorage.getItem('rdpmanager-theme-mode');
    return saved === 'light' ? 'light' : 'dark';
  });

  const [viewMode, setViewMode] = useState<ViewMode>(() => {
    const saved = window.localStorage.getItem('rdpmanager-view-mode');
    return saved === 'table' ? 'table' : 'grid';
  });

  // State
  const [connections, setConnections] = useState<ConnectionProfile[]>([]);
  const [selectedIndex, setSelectedIndex] = useState<number | null>(null);
  const [appInfo, setAppInfo] = useState<AppInfo | null>(null);
  const [databaseStatus, setDatabaseStatus] = useState<DatabaseStatus>({ isOpen: false, path: '' });
  const [openDatabases, setOpenDatabases] = useState<string[]>([]);
  const [activeDatabase, setActiveDatabase] = useState('');
  const [folders, setFolders] = useState<string[]>([]);
  const [importedRdpData, setImportedRdpData] = useState<Record<string, unknown> | null>(null);
  const [selectedFolder, setSelectedFolder] = useState('');
  const [treeWidth, setTreeWidth] = useState(260);
  const [draggingConnectionName, setDraggingConnectionName] = useState('');

  const isResizingRef = useRef(false);

  // Dialog state
  const [editorOpen, setEditorOpen] = useState(false);
  const [editorProfile, setEditorProfile] = useState<ConnectionProfile>(defaultConnectionProfile());
  const [editorIsNew, setEditorIsNew] = useState(true);

  const [certDialogOpen, setCertDialogOpen] = useState(false);
  const [certInfo, setCertInfo] = useState<CertificateInfo | null>(null);

  const [authDialogOpen, setAuthDialogOpen] = useState(false);
  const [authRequest, setAuthRequest] = useState<AuthRequest | null>(null);

  const [manualCodeFlowOpen, setManualCodeFlowOpen] = useState(false);
  const [manualCodeFlowInfo, setManualCodeFlowInfo] = useState<ManualCodeFlowInfo | null>(null);

  const [deleteDialogOpen, setDeleteDialogOpen] = useState(false);
  const [deleteTargetName, setDeleteTargetName] = useState('');

  // Feed discovery state
  const [feedAccounts, setFeedAccounts] = useState<FeedAccount[]>([]);
  const [discoveryInProgress, setDiscoveryInProgress] = useState(false);

  // Account action dialog state
  const [accountActionOpen, setAccountActionOpen] = useState(false);
  const [accountAction, setAccountAction] = useState<AccountAction>('logoff');
  const [accountActionTarget, setAccountActionTarget] = useState<{ id: string; name: string }>({ id: '', name: '' });

  // Folder settings dialog state
  const [folderSettingsOpen, setFolderSettingsOpen] = useState(false);
  const [folderSettingsPath, setFolderSettingsPath] = useState('');

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

  // Load database status
  const loadDatabaseStatus = useCallback(async () => {
    const status = await apiGetDatabaseStatus();
    setDatabaseStatus(status);
  }, []);

  // Load feed accounts
  const loadFeedAccounts = useCallback(async () => {
    const accounts = await apiGetFeedAccounts();
    setFeedAccounts(accounts);
  }, []);

  const ensureDatabaseTab = useCallback((path: string) => {
    if (!path) {
      return;
    }
    setOpenDatabases((current) => (current.includes(path) ? current : [...current, path]));
    setActiveDatabase(path);
  }, []);

  const loadFolders = useCallback(async () => {
    const values = await apiGetFolders();
    setFolders(values);
  }, []);

  // Connect
  const handleConnect = useCallback(
    async (conn: ConnectionProfile) => {
      if (!conn.hostname) {
        showToast('Please enter a hostname or IP address', 'error');
        return;
      }

      // Resolve effective profile (applies folder inheritance) before connecting
      const resolved = await apiGetEffectiveConnectionProfile(conn.name);
      const params: ConnectionParams = {
        ...(resolved || conn),
      };

      const result = await apiConnect(params);
      if (result.success) {
        showToast(`Connecting to ${conn.hostname}...`, 'success');
      } else {
        showToast(`Connection failed: ${result.error}`, 'error');
      }
    },
    [showToast]
  );

  // Save
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
      } else {
        showToast('Failed to save connection', 'error');
      }
    },
    [showToast, loadConnections]
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
        overridden_fields: [...INHERITABLE_FIELDS],  // Imported profiles override all fields
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
    const profile = defaultConnectionProfile();
    profile.folder = selectedFolder;
    setEditorProfile(profile);
    setEditorIsNew(true);
    setEditorOpen(true);
    setImportedRdpData(null);
  }, [selectedFolder]);

  const connectionIndexByName = useCallback(
    (name: string) => connections.findIndex((conn) => conn.name === name),
    [connections]
  );

  const handleEditConnection = useCallback(
    (index: number) => {
      const filtered = selectedFolder
        ? connections.filter((conn) => (conn.folder || '') === selectedFolder)
        : connections;
      const conn = filtered[index];
      const globalIndex = conn ? connectionIndexByName(conn.name) : -1;
      if (globalIndex < 0) {
        return;
      }
      setEditorProfile({ ...connections[globalIndex] });
      setEditorIsNew(false);
      setEditorOpen(true);
    },
    [connectionIndexByName, connections, selectedFolder]
  );

  const handleRequestDelete = useCallback(
    (index: number) => {
      const filtered = selectedFolder
        ? connections.filter((conn) => (conn.folder || '') === selectedFolder)
        : connections;
      const conn = filtered[index];
      if (!conn) {
        return;
      }
      setDeleteTargetName(conn.name);
      setDeleteDialogOpen(true);
    },
    [connections, selectedFolder]
  );

  const handleDoubleClick = useCallback(
    (index: number) => {
      const filtered = selectedFolder
        ? connections.filter((conn) => (conn.folder || '') === selectedFolder)
        : connections;
      if (!filtered[index]) {
        return;
      }
      setSelectedIndex(index);
      handleConnect(filtered[index]);
    },
    [connections, handleConnect, selectedFolder]
  );

  const handleContextConnect = useCallback(
    (index: number) => {
      const filtered = selectedFolder
        ? connections.filter((conn) => (conn.folder || '') === selectedFolder)
        : connections;
      if (!filtered[index]) {
        return;
      }
      setSelectedIndex(index);
      handleConnect(filtered[index]);
    },
    [connections, handleConnect, selectedFolder]
  );

  const handleCreateFolder = useCallback(() => {
    const value = window.prompt('Folder name (supports nesting, e.g. Work/Prod):', '');
    const normalized = (value || '')
      .split('/')
      .map((part) => part.trim())
      .filter(Boolean)
      .join('/');

    if (!normalized) {
      return;
    }

    void (async () => {
      const success = await apiCreateFolder(normalized);
      if (!success) {
        showToast('Failed to create folder', 'error');
        return;
      }

      await loadFolders();
      setSelectedFolder(normalized);
      showToast(`Folder created: ${normalized}`, 'success');
    })();
  }, [loadFolders, showToast]);

  const handleDropConnectionToFolder = useCallback(
    async (folder: string, droppedConnectionName: string) => {
      const connectionName = droppedConnectionName || draggingConnectionName;
      if (!connectionName) {
        return;
      }

      const conn = connections.find((item) => item.name === connectionName);
      if (!conn) {
        setDraggingConnectionName('');
        return;
      }

      if ((conn.folder || '') === folder) {
        setDraggingConnectionName('');
        return;
      }

      const updated: ConnectionProfile = { ...conn, folder };
      const success = await apiSaveConnection(updated);
      if (!success) {
        showToast('Failed to move connection to folder', 'error');
        setDraggingConnectionName('');
        return;
      }

      await loadConnections();
      setDraggingConnectionName('');

      const destination = folder || 'All Connections';
      showToast(`Moved "${updated.name}" to ${destination}`, 'success');
    },
    [connections, draggingConnectionName, loadConnections, showToast]
  );

  const handleDropFolderToFolder = useCallback(
    async (sourceFolder: string, targetParentFolder: string) => {
      if (!sourceFolder) {
        return;
      }

      if (sourceFolder === targetParentFolder) {
        return;
      }

      if (targetParentFolder.startsWith(sourceFolder + '/')) {
        showToast('Cannot move a folder inside itself', 'warning');
        return;
      }

      const success = await apiMoveFolder(sourceFolder, targetParentFolder);
      if (!success) {
        showToast('Failed to move folder', 'error');
        return;
      }

      await loadConnections();
      await loadFolders();

      const sourceParts = sourceFolder.split('/');
      const sourceName = sourceParts[sourceParts.length - 1];
      const nextSelected = targetParentFolder ? `${targetParentFolder}/${sourceName}` : sourceName;
      setSelectedFolder(nextSelected);
      showToast(`Folder moved to ${targetParentFolder || 'root'}`, 'success');
    },
    [loadConnections, loadFolders, showToast]
  );

  const handleRenameFolder = useCallback(
    async (folderPath: string) => {
      const oldParts = folderPath.split('/');
      const currentName = oldParts[oldParts.length - 1];
      const newNameRaw = window.prompt('New folder name:', currentName);
      const newName = (newNameRaw || '').trim();

      if (!newName || newName === currentName || newName.includes('/')) {
        return;
      }

      const success = await apiRenameFolder(folderPath, newName);
      if (!success) {
        showToast('Failed to rename folder', 'error');
        return;
      }

      await loadConnections();
      await loadFolders();

      const parent = oldParts.slice(0, -1).join('/');
      const nextPath = parent ? `${parent}/${newName}` : newName;
      if (selectedFolder === folderPath || selectedFolder.startsWith(folderPath + '/')) {
        setSelectedFolder(nextPath + selectedFolder.slice(folderPath.length));
      }
      showToast('Folder renamed', 'success');
    },
    [loadConnections, loadFolders, selectedFolder, showToast]
  );

  const handleDeleteFolder = useCallback(
    async (folderPath: string) => {
      const confirmed = window.confirm(
        `Delete folder "${folderPath}"? This will delete all subfolders and their connections.`
      );
      if (!confirmed) {
        return;
      }

      const success = await apiDeleteFolder(folderPath);
      if (!success) {
        showToast('Failed to delete folder', 'error');
        return;
      }

      await loadConnections();
      await loadFolders();

      if (selectedFolder === folderPath || selectedFolder.startsWith(folderPath + '/')) {
        setSelectedFolder('');
        setSelectedIndex(null);
      }

      showToast('Folder and nested connections deleted', 'success');
    },
    [loadConnections, loadFolders, selectedFolder, showToast]
  );

  const handleCreateDatabase = useCallback(async () => {
    const success = await apiCreateDatabaseDialog();
    if (!success) {
      return;
    }

    await loadDatabaseStatus();
    await loadConnections();
    await loadFolders();
    const status = await apiGetDatabaseStatus();
    if (status.isOpen && status.path) {
      ensureDatabaseTab(status.path);
    }
    showToast('Database created and opened', 'success');
  }, [ensureDatabaseTab, loadConnections, loadDatabaseStatus, loadFolders, showToast]);

  const handleOpenDatabase = useCallback(async () => {
    const success = await apiOpenDatabaseDialog();
    if (!success) {
      return;
    }

    await loadDatabaseStatus();
    await loadConnections();
    await loadFolders();
    const status = await apiGetDatabaseStatus();
    if (status.isOpen && status.path) {
      ensureDatabaseTab(status.path);
    }
    showToast('Database opened', 'success');
  }, [ensureDatabaseTab, loadConnections, loadDatabaseStatus, loadFolders, showToast]);

  const handleCloneDatabase = useCallback(
    async (sourcePath: string) => {
      const suggested = sourcePath.endsWith('.db')
        ? `${sourcePath.slice(0, -3)}-copy.db`
        : `${sourcePath}-copy`;
      const targetPath = window.prompt('Clone database to path:', suggested);
      if (!targetPath || !targetPath.trim()) {
        return;
      }

      const success = await apiCloneDatabase(sourcePath, targetPath.trim());
      if (!success) {
        showToast('Failed to clone database', 'error');
        return;
      }

      await loadDatabaseStatus();
      await loadConnections();
      await loadFolders();
      ensureDatabaseTab(targetPath.trim());
      showToast('Database cloned and opened', 'success');
    },
    [ensureDatabaseTab, loadConnections, loadDatabaseStatus, loadFolders, showToast]
  );

  const handleCopyDatabasePath = useCallback(
    async (path: string) => {
      const value = path.trim();
      if (!value) {
        return;
      }

      try {
        if (navigator.clipboard?.writeText) {
          await navigator.clipboard.writeText(value);
        } else {
          const textarea = document.createElement('textarea');
          textarea.value = value;
          textarea.setAttribute('readonly', '');
          textarea.style.position = 'fixed';
          textarea.style.opacity = '0';
          document.body.appendChild(textarea);
          textarea.select();
          document.execCommand('copy');
          document.body.removeChild(textarea);
        }
        showToast('Database path copied', 'success');
      } catch {
        showToast('Failed to copy database path', 'error');
      }
    },
    [showToast]
  );

  const handleCloseDatabase = useCallback(async () => {
    const success = await apiCloseDatabase();
    if (!success) {
      showToast('Failed to close database', 'error');
      return;
    }

    await loadDatabaseStatus();
    await loadConnections();
    await loadFolders();
    setSelectedIndex(null);
    setOpenDatabases([]);
    setActiveDatabase('');
    showToast('Database closed', 'info');
  }, [loadConnections, loadDatabaseStatus, loadFolders, showToast]);

  const handleSelectDatabaseTab = useCallback(
    async (path: string) => {
      if (!path || path === activeDatabase) {
        return;
      }

      const success = await apiOpenDatabase(path);
      if (!success) {
        showToast('Failed to switch database', 'error');
        return;
      }

      await loadDatabaseStatus();
      await loadConnections();
      await loadFolders();
      setActiveDatabase(path);
      setSelectedFolder('');
      setSelectedIndex(null);
    },
    [activeDatabase, loadConnections, loadDatabaseStatus, loadFolders, showToast]
  );

  const handleCloseDatabaseTab = useCallback(
    async (path: string) => {
      if (!path) {
        return;
      }

      const remaining = openDatabases.filter((dbPath) => dbPath !== path);

      if (path !== activeDatabase) {
        setOpenDatabases(remaining);
        return;
      }

      if (remaining.length === 0) {
        await handleCloseDatabase();
        return;
      }

      const nextPath = remaining[remaining.length - 1];
      const success = await apiOpenDatabase(nextPath);
      if (!success) {
        showToast('Failed to switch database after closing tab', 'error');
        return;
      }

      setOpenDatabases(remaining);
      setActiveDatabase(nextPath);
      await loadDatabaseStatus();
      await loadConnections();
      await loadFolders();
      setSelectedFolder('');
      setSelectedIndex(null);
    },
    [activeDatabase, handleCloseDatabase, loadConnections, loadDatabaseStatus, loadFolders, openDatabases, showToast]
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

    (window as unknown as Record<string, unknown>).showManualCodeFlowDialog = (info: ManualCodeFlowInfo) => {
      setManualCodeFlowInfo(info);
      setManualCodeFlowOpen(true);
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
      await loadDatabaseStatus();
      await loadConnections();
      await loadFolders();
      await loadFeedAccounts();
      console.log('[RDPMAN] Initial data loaded.');
    };

    // Feed discovery progress/completion callbacks
    (window as unknown as Record<string, unknown>).onFeedDiscoveryProgress = (
      data: { message: string; current: number; total: number }
    ) => {
      dispatchToast(
        <Toast>
          <ToastTitle>{data.message}</ToastTitle>
        </Toast>,
        { intent: 'info', timeout: 3000, position: 'bottom-end' }
      );
    };

    (window as unknown as Record<string, unknown>).onFeedDiscoveryComplete = async (
      data: { success: boolean; imported_count: number; tenant_count: number; message: string }
    ) => {
      setDiscoveryInProgress(false);
      await loadConnections();
      await loadFolders();
      await loadFeedAccounts();
      dispatchToast(
        <Toast>
          <ToastTitle>{data.message}</ToastTitle>
        </Toast>,
        { intent: data.success ? 'success' : 'error', timeout: 6000, position: 'bottom-end' }
      );
    };

    // showToast global for C++ backend usage
    (window as unknown as Record<string, unknown>).showToast = (
      message: string, intent: string, _timeout?: number
    ) => {
      dispatchToast(
        <Toast>
          <ToastTitle>{message}</ToastTitle>
        </Toast>,
        { intent: (intent as 'info' | 'success' | 'error' | 'warning') || 'info', timeout: 4000, position: 'bottom-end' }
      );
    };
  }, [dispatchToast, loadAppInfo, loadConnections, loadDatabaseStatus, loadFolders, loadFeedAccounts]);

  // Keyboard shortcut: Ctrl+N for new connection
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement | null;
      const isTypingContext = !!target && (
        target.tagName === 'INPUT' ||
        target.tagName === 'TEXTAREA' ||
        target.tagName === 'SELECT' ||
        target.isContentEditable
      );

      if (e.ctrlKey && e.key === 'n') {
        e.preventDefault();
        handleNewConnection();
        return;
      }

      if (isTypingContext || !selectedFolder) {
        return;
      }

      if (e.key === 'F2') {
        e.preventDefault();
        void handleRenameFolder(selectedFolder);
        return;
      }

      if (e.key === 'Delete') {
        e.preventDefault();
        void handleDeleteFolder(selectedFolder);
      }
    };
    document.addEventListener('keydown', handler);
    return () => document.removeEventListener('keydown', handler);
  }, [handleDeleteFolder, handleNewConnection, handleRenameFolder, selectedFolder]);

  useEffect(() => {
    window.localStorage.setItem('rdpmanager-theme-mode', themeMode);
  }, [themeMode]);

  useEffect(() => {
    if (databaseStatus.isOpen && databaseStatus.path) {
      ensureDatabaseTab(databaseStatus.path);
    }
  }, [databaseStatus, ensureDatabaseTab]);

  useEffect(() => {
    if (databaseStatus.isOpen) {
      return;
    }

    setConnections([]);
    setFolders([]);
    setSelectedFolder('');
    setSelectedIndex(null);
    setDraggingConnectionName('');
    setImportedRdpData(null);
    setOpenDatabases([]);
    setActiveDatabase('');
  }, [databaseStatus.isOpen]);

  useEffect(() => {
    const onMouseMove = (event: MouseEvent) => {
      if (!isResizingRef.current) {
        return;
      }

      const min = 180;
      const max = 520;
      const next = Math.max(min, Math.min(max, event.clientX - 16));
      setTreeWidth(next);
    };

    const onMouseUp = () => {
      isResizingRef.current = false;
    };

    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', onMouseUp);
    return () => {
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
    };
  }, []);

  const visibleConnections = selectedFolder
    ? connections.filter((conn) => (conn.folder || '') === selectedFolder)
    : connections;
  const mainVisible = databaseStatus.isOpen;

  const footerText = appInfo
    ? `${appInfo.name} v${appInfo.version} | FreeRDP ${appInfo.freerdp_version}`
    : 'WebUI RDP Client v0.1.0';

  const databaseText = databaseStatus.isOpen
    ? `Database: ${databaseStatus.path}`
    : 'Database: (closed)';

  return (
    <FluentProvider theme={getFluentTheme(themeMode)} style={{ height: '100%' }}>
      <div className={styles.root}>
        <AppHeader
          themeMode={themeMode}
          onThemeModeChange={setThemeMode}
        />
        <Toolbar
          onNewConnection={handleNewConnection}
          onImportFile={handleImportFile}
          onCreateDatabase={handleCreateDatabase}
          onOpenDatabase={handleOpenDatabase}
          onCloseDatabase={handleCloseDatabase}
          databaseOpen={databaseStatus.isOpen}
          feedAccounts={feedAccounts}
          onAddAccount={async () => {
            setDiscoveryInProgress(true);
            const result = await apiDiscoverFeeds('');
            setDiscoveryInProgress(false);
            await loadConnections();
            await loadFolders();
            await loadFeedAccounts();
            if (!result.success) {
              showToast(result.error || 'Feed discovery failed', 'error');
            }
          }}
          onLogOffAccount={(id) => {
            const acct = feedAccounts.find(a => a.id === id);
            setAccountAction('logoff');
            setAccountActionTarget({ id, name: acct?.display_name ?? id });
            setAccountActionOpen(true);
          }}
          onForgetAccount={(id) => {
            const acct = feedAccounts.find(a => a.id === id);
            setAccountAction('forget');
            setAccountActionTarget({ id, name: acct?.display_name ?? id });
            setAccountActionOpen(true);
          }}
          onDiscoverFeeds={async (account) => {
            setDiscoveryInProgress(true);
            const result = await apiDiscoverFeeds(account.id);
            setDiscoveryInProgress(false);
            await loadConnections();
            await loadFolders();
            await loadFeedAccounts();
            if (!result.success) {
              showToast(result.error || 'Feed discovery failed', 'error');
            }
          }}
          discoveryInProgress={discoveryInProgress}
          viewMode={viewMode}
          onViewModeChange={(mode) => {
            setViewMode(mode);
            window.localStorage.setItem('rdpmanager-view-mode', mode);
          }}
        />
        <DatabaseTabs
          databases={openDatabases}
          activeDatabase={activeDatabase}
          onSelect={handleSelectDatabaseTab}
          onClose={handleCloseDatabaseTab}
          onClone={handleCloneDatabase}
          onCopyPath={handleCopyDatabasePath}
        />
        {mainVisible && (
          <div className={styles.main}>
            <div className={styles.leftPane} style={{ width: `${treeWidth}px` }}>
              <FolderTree
                connections={connections}
                folders={folders}
                selectedFolder={selectedFolder}
                onSelectFolder={(folder) => {
                  setSelectedFolder(folder);
                  setSelectedIndex(null);
                }}
                onCreateFolder={handleCreateFolder}
                onDropConnectionToFolder={handleDropConnectionToFolder}
                onDropFolderToFolder={handleDropFolderToFolder}
                onRenameFolder={handleRenameFolder}
                onDeleteFolder={handleDeleteFolder}
                onEditFolderDefaults={(path) => {
                  setFolderSettingsPath(path);
                  setFolderSettingsOpen(true);
                }}
              />
            </div>
            <div
              className={styles.splitter}
              onMouseDown={() => {
                isResizingRef.current = true;
              }}
              role="separator"
              aria-orientation="vertical"
              aria-label="Resize folder tree"
            />
            <div className={styles.contentPane}>
              {viewMode === 'table' ? (
                <ConnectionTable
                  connections={visibleConnections}
                  selectedIndex={selectedIndex}
                  onSelect={setSelectedIndex}
                  onDoubleClick={handleDoubleClick}
                  onConnect={handleContextConnect}
                  onEdit={handleEditConnection}
                  onDelete={handleRequestDelete}
                  onDragStartConnection={setDraggingConnectionName}
                />
              ) : (
                <ConnectionGrid
                  connections={visibleConnections}
                  selectedIndex={selectedIndex}
                  onSelect={setSelectedIndex}
                  onDoubleClick={handleDoubleClick}
                  onConnect={handleContextConnect}
                  onEdit={handleEditConnection}
                  onDelete={handleRequestDelete}
                  onDragStartConnection={setDraggingConnectionName}
                />
              )}
            </div>
          </div>
        )}
        <div className={styles.footer}>{`${footerText} | ${databaseText}`}</div>

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

        <ManualCodeFlowDialog
          open={manualCodeFlowOpen}
          info={manualCodeFlowInfo}
          onSubmit={(redirectUrl) => {
            setManualCodeFlowOpen(false);
            apiAadAuthResponse(true, redirectUrl);
          }}
          onCancel={() => {
            setManualCodeFlowOpen(false);
            apiAadAuthResponse(false, '');
          }}
        />

        <DeleteDialog
          open={deleteDialogOpen}
          connectionName={deleteTargetName}
          onConfirm={() => handleDeleteConnection(deleteTargetName)}
          onCancel={() => setDeleteDialogOpen(false)}
        />

        <AccountActionDialog
          open={accountActionOpen}
          action={accountAction}
          accountName={accountActionTarget.name}
          onCancel={() => setAccountActionOpen(false)}
          onConfirm={async () => {
            setAccountActionOpen(false);
            const { id } = accountActionTarget;
            if (accountAction === 'logoff') {
              const success = await apiLogOffAccount(id);
              if (success) {
                showToast('Logged off — cached tokens cleared', 'info');
                await loadFeedAccounts();
              } else {
                showToast('Failed to log off account', 'error');
              }
            } else {
              const success = await apiForgetAccount(id);
              if (success) {
                showToast('Account forgotten', 'info');
                await loadFeedAccounts();
                await loadConnections();
                await loadFolders();
              } else {
                showToast('Failed to forget account', 'error');
              }
            }
          }}
        />

        <FolderSettingsDialog
          open={folderSettingsOpen}
          folderPath={folderSettingsPath}
          onSave={async (path, settings) => {
            const success = await apiSaveFolderSettings(path, settings);
            if (success) {
              showToast('Folder defaults saved', 'success');
            } else {
              showToast('Failed to save folder defaults', 'error');
            }
            setFolderSettingsOpen(false);
          }}
          onCancel={() => setFolderSettingsOpen(false)}
        />
      </div>
    </FluentProvider>
  );
}
