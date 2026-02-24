/**
 * WebUI type declarations.
 * WebUI injects `webui.js` at runtime which provides the `webui` global
 * and registers bound C++ functions as global async functions.
 */

interface WebUI {
  call(fn: string, ...args: unknown[]): Promise<unknown>;
}

declare const webui: WebUI;

// Global functions bound by the C++ backend via WebUI
declare function connectRDP(jsonParams: string): Promise<string>;
declare function getConnections(): Promise<string>;
declare function saveConnection(jsonParams: string): Promise<boolean>;
declare function deleteConnection(name: string): Promise<boolean>;
declare function getAppInfo(): Promise<string>;
declare function importRdpFile(content: string): Promise<string>;
declare function createDatabase(path: string): Promise<boolean>;
declare function openDatabase(path: string): Promise<boolean>;
declare function closeDatabase(): Promise<boolean>;
declare function getDatabaseStatus(): Promise<string>;
declare function createFolder(path: string): Promise<boolean>;
declare function moveFolder(sourcePath: string, targetParentPath: string): Promise<boolean>;
declare function renameFolder(sourcePath: string, newName: string): Promise<boolean>;
declare function deleteFolder(path: string): Promise<boolean>;
declare function getFolders(): Promise<string>;
declare function certificateResponse(choice: number): Promise<void>;
declare function authResponse(
  success: boolean,
  username: string,
  password: string,
  domain: string
): Promise<void>;
declare function aadAuthResponse(
  success: boolean,
  redirectUrl: string
): Promise<void>;
