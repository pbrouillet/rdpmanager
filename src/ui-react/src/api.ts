/**
 * API layer for communicating with the C++ backend via WebUI bindings.
 */
import type {
  ConnectionProfile,
  ConnectionParams,
  ConnectResult,
  AppInfo,
  ImportResult,
  DatabaseStatus,
  FeedAccount,
  DiscoverResult,
  FolderSettings,
} from './types';

export async function apiGetConnections(): Promise<ConnectionProfile[]> {
  try {
    const result = await getConnections();
    if (!result) return [];
    return JSON.parse(result) as ConnectionProfile[];
  } catch {
    console.error('[RDPMAN] Failed to load connections');
    return [];
  }
}

export async function apiGetAppInfo(): Promise<AppInfo | null> {
  try {
    const result = await getAppInfo();
    return JSON.parse(result) as AppInfo;
  } catch {
    console.error('[RDPMAN] Failed to load app info');
    return null;
  }
}

export async function apiConnect(params: ConnectionParams): Promise<ConnectResult> {
  try {
    const result = await connectRDP(JSON.stringify(params));
    return JSON.parse(result) as ConnectResult;
  } catch (error) {
    return { success: false, error: String(error) };
  }
}

export async function apiSaveConnection(profile: ConnectionProfile): Promise<boolean> {
  try {
    return await saveConnection(JSON.stringify(profile));
  } catch {
    return false;
  }
}

export async function apiDeleteConnection(name: string): Promise<boolean> {
  try {
    return await deleteConnection(name);
  } catch {
    return false;
  }
}

export async function apiImportRdpFile(content: string): Promise<ImportResult> {
  try {
    const result = await importRdpFile(content);
    return JSON.parse(result) as ImportResult;
  } catch {
    return { success: false, error: 'Failed to parse RDP file' };
  }
}

export async function apiCreateDatabase(path: string): Promise<boolean> {
  try {
    return await createDatabase(path);
  } catch {
    return false;
  }
}

export async function apiCreateDatabaseDialog(): Promise<boolean> {
  try {
    return await createDatabaseDialog();
  } catch {
    return false;
  }
}

export async function apiOpenDatabase(path: string): Promise<boolean> {
  try {
    return await openDatabase(path);
  } catch {
    return false;
  }
}

export async function apiOpenDatabaseDialog(): Promise<boolean> {
  try {
    return await openDatabaseDialog();
  } catch {
    return false;
  }
}

export async function apiCloneDatabase(sourcePath: string, targetPath: string): Promise<boolean> {
  try {
    return await cloneDatabase(sourcePath, targetPath);
  } catch {
    return false;
  }
}

export async function apiCloseDatabase(): Promise<boolean> {
  try {
    return await closeDatabase();
  } catch {
    return false;
  }
}

export async function apiGetDatabaseStatus(): Promise<DatabaseStatus> {
  try {
    const result = await getDatabaseStatus();
    return JSON.parse(result) as DatabaseStatus;
  } catch {
    return { isOpen: false, path: '' };
  }
}

export async function apiCreateFolder(path: string): Promise<boolean> {
  try {
    return await createFolder(path);
  } catch {
    return false;
  }
}

export async function apiMoveFolder(sourcePath: string, targetParentPath: string): Promise<boolean> {
  try {
    return await moveFolder(sourcePath, targetParentPath);
  } catch {
    return false;
  }
}

export async function apiRenameFolder(sourcePath: string, newName: string): Promise<boolean> {
  try {
    return await renameFolder(sourcePath, newName);
  } catch {
    return false;
  }
}

export async function apiDeleteFolder(path: string): Promise<boolean> {
  try {
    return await deleteFolder(path);
  } catch {
    return false;
  }
}

export async function apiGetFolders(): Promise<string[]> {
  try {
    const result = await getFolders();
    if (!result) {
      return [];
    }
    return JSON.parse(result) as string[];
  } catch {
    return [];
  }
}

export function apiCertificateResponse(choice: number): void {
  certificateResponse(choice);
}

export function apiAuthResponse(
  success: boolean,
  username: string,
  password: string,
  domain: string
): void {
  authResponse(success, username, password, domain);
}

export function apiAadAuthResponse(success: boolean, redirectUrl: string): void {
  aadAuthResponse(success, redirectUrl);
}

// ============================================================================
// Feed Discovery
// ============================================================================

export async function apiGetFeedAccounts(): Promise<FeedAccount[]> {
  try {
    const result = await getFeedAccounts();
    if (!result) return [];
    return JSON.parse(result) as FeedAccount[];
  } catch {
    console.error('[RDPMAN] Failed to load feed accounts');
    return [];
  }
}

export async function apiDeleteFeedAccount(id: string): Promise<boolean> {
  try {
    return await deleteFeedAccount(id);
  } catch {
    return false;
  }
}

export async function apiDiscoverFeeds(accountId: string): Promise<DiscoverResult> {
  try {
    const result = await discoverFeeds(accountId);
    return JSON.parse(result) as DiscoverResult;
  } catch (error) {
    return { success: false, error: String(error), imported_count: 0, tenant_count: 0, account_id: '', account_display_name: '' };
  }
}

export async function apiLogOffAccount(id: string): Promise<boolean> {
  try {
    return await logOffAccount(id);
  } catch {
    return false;
  }
}

export async function apiForgetAccount(id: string): Promise<boolean> {
  try {
    return await forgetAccount(id);
  } catch {
    return false;
  }
}

// ============================================================================
// Folder Settings (parameter inheritance)
// ============================================================================

export async function apiGetFolderSettings(path: string): Promise<FolderSettings> {
  try {
    const result = await getFolderSettings(path);
    return result ? (JSON.parse(result) as FolderSettings) : {};
  } catch {
    return {};
  }
}

export async function apiSaveFolderSettings(path: string, settings: FolderSettings): Promise<boolean> {
  try {
    return await saveFolderSettings(path, JSON.stringify(settings));
  } catch {
    return false;
  }
}

export async function apiGetEffectiveFolderSettings(path: string): Promise<FolderSettings> {
  try {
    const result = await getEffectiveFolderSettings(path);
    return result ? (JSON.parse(result) as FolderSettings) : {};
  } catch {
    return {};
  }
}

export async function apiGetEffectiveConnectionProfile(name: string): Promise<ConnectionProfile | null> {
  try {
    const result = await getEffectiveConnectionProfile(name);
    return result ? (JSON.parse(result) as ConnectionProfile) : null;
  } catch {
    return null;
  }
}
