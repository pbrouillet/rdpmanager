export interface ConnectionProfile {
  name: string;
  folder: string;
  hostname: string;
  port: number;
  username: string;
  domain: string;
  // Advanced options
  home_drive: boolean;
  clipboard: boolean;
  cert_tofu: boolean;
  usb_auto: boolean;
  floatbar: boolean;
  dynamic_resolution: boolean;
  network_auto: boolean;
  gfx_avc420: boolean;
  compression: boolean;
  audio_pulse: boolean;
  prevent_session_lock: boolean;
  auto_reconnect: boolean;
  auto_reconnect_max_retries: number;
  // Gateway / AVD options
  gateway_hostname: string;
  enable_rds_aad_auth: boolean;
  target_is_aad_joined: boolean;
  load_balance_info: string;
  // AVD-specific fields
  remote_desktop_name: string;
  wvd_endpoint_pool: string;
  workspace_id: string;
  arm_path: string;
  aad_tenant_id: string;
  remote_application_program: string;
}

export interface ConnectionParams extends ConnectionProfile {
}

export interface CertificateInfo {
  host: string;
  port: number;
  commonName: string;
  subject: string;
  issuer: string;
  fingerprint: string;
  isChanged: boolean;
  oldFingerprint: string;
}

export interface AuthRequest {
  hostname: string;
  isGateway: boolean;
  currentUsername: string;
  currentDomain: string;
}

export interface AppInfo {
  name: string;
  version: string;
  freerdp_version: string;
}

export interface DatabaseStatus {
  isOpen: boolean;
  path: string;
}

export interface ConnectResult {
  success: boolean;
  error?: string;
}

export interface ImportResult {
  success: boolean;
  error?: string;
  data?: RdpFileData;
}

export interface RdpFileData {
  full_address: string;
  server_port: number;
  username: string;
  domain: string;
  display_name: string;
  remote_desktop_name: string;
  dynamic_resolution: boolean;
  redirect_clipboard: boolean;
  gateway_hostname: string;
  enable_rds_aad_auth: boolean;
  target_is_aad_joined: boolean;
  load_balance_info: string;
  aad_tenant_id: string;
  wvd_endpoint_pool: string;
  workspace_id: string;
  arm_path: string;
  remote_application_program: string;
  is_avd_connection: boolean;
  uses_gateway: boolean;
}

export interface FeedAccount {
  id: string;
  display_name: string;
  email: string;
  last_synced: number;
}

export interface DiscoverResult {
  success: boolean;
  error?: string;
  imported_count: number;
  tenant_count: number;
  account_id: string;
  account_display_name: string;
}

export function defaultConnectionProfile(): ConnectionProfile {
  return {
    name: '',
    folder: '',
    hostname: '',
    port: 3389,
    username: '',
    domain: '',
    home_drive: false,
    clipboard: true,
    cert_tofu: false,
    usb_auto: false,
    floatbar: false,
    dynamic_resolution: false,
    network_auto: false,
    gfx_avc420: false,
    compression: false,
    audio_pulse: false,
    prevent_session_lock: false,
    auto_reconnect: false,
    auto_reconnect_max_retries: 3,
    gateway_hostname: '',
    enable_rds_aad_auth: false,
    target_is_aad_joined: false,
    load_balance_info: '',
    remote_desktop_name: '',
    wvd_endpoint_pool: '',
    workspace_id: '',
    arm_path: '',
    aad_tenant_id: '',
    remote_application_program: '',
  };
}
