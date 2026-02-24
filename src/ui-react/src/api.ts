/**
 * API layer for communicating with the C++ backend via WebUI bindings.
 */
import type {
  ConnectionProfile,
  ConnectionParams,
  ConnectResult,
  AppInfo,
  ImportResult,
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
