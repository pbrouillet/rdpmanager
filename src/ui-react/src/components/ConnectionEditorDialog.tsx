import { useState, useEffect, useCallback } from 'react';
import {
  Dialog,
  DialogSurface,
  DialogBody,
  DialogTitle,
  DialogContent,
  DialogActions,
  Button,
  Input,
  Label,
  Checkbox,
  SpinButton,
  Switch,
  makeStyles,
  tokens,
  Accordion,
  AccordionItem,
  AccordionHeader,
  AccordionPanel,
  Divider,
  Text,
  Tooltip,
} from '@fluentui/react-components';
import { Dismiss24Regular } from '@fluentui/react-icons';
import type { ConnectionProfile, FolderSettings } from '../types';
import { INHERITABLE_FIELDS } from '../types';
import { apiGetEffectiveFolderSettings } from '../api';

const useStyles = makeStyles({
  form: {
    display: 'flex',
    flexDirection: 'column',
    gap: tokens.spacingVerticalM,
  },
  row: {
    display: 'flex',
    flexWrap: 'wrap',
    gap: tokens.spacingHorizontalM,
  },
  field: {
    flex: 1,
    display: 'flex',
    flexDirection: 'column',
    gap: tokens.spacingVerticalXS,
  },
  fieldSmall: {
    flex: '0 0 120px',
    minWidth: '120px',
    display: 'flex',
    flexDirection: 'column',
    gap: tokens.spacingVerticalXS,
  },
  checkboxGrid: {
    display: 'grid',
    gridTemplateColumns: 'repeat(auto-fill, minmax(190px, 1fr))',
    gap: tokens.spacingVerticalXS,
  },
  groupTitle: {
    fontSize: tokens.fontSizeBase200,
    fontWeight: tokens.fontWeightSemibold,
    color: tokens.colorBrandForeground1,
    textTransform: 'uppercase' as const,
    letterSpacing: '0.5px',
    marginBottom: tokens.spacingVerticalXS,
  },
  group: {
    marginBottom: tokens.spacingVerticalS,
  },
  surface: {
    width: 'min(680px, calc(100vw - 24px))',
    maxWidth: '680px',
    maxHeight: 'calc(100vh - 24px)',
  },
  content: {
    overflowY: 'auto',
    maxHeight: 'calc(100vh - 220px)',
    minHeight: '220px',
    paddingRight: tokens.spacingHorizontalXS,
    paddingBottom: tokens.spacingVerticalS,
  },
  settingRow: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalS,
    minHeight: '32px',
    paddingBottom: tokens.spacingVerticalXXS,
  },
  setSwitch: {
    flexShrink: 0,
  },
  inheritedHint: {
    fontSize: tokens.fontSizeBase100,
    color: tokens.colorNeutralForeground3,
    fontStyle: 'italic',
  },
});

interface ConnectionEditorDialogProps {
  open: boolean;
  profile: ConnectionProfile;
  isNew: boolean;
  onSave: (profile: ConnectionProfile) => void;
  onCancel: () => void;
}

export function ConnectionEditorDialog({
  open,
  profile,
  isNew,
  onSave,
  onCancel,
}: ConnectionEditorDialogProps) {
  const styles = useStyles();
  const [form, setForm] = useState<ConnectionProfile>(profile);
  const [overrides, setOverrides] = useState<Set<string>>(new Set());
  const [inherited, setInherited] = useState<FolderSettings>({});

  useEffect(() => {
    if (!open) return;
    setForm({
      ...profile,
      save_password: !!profile.encrypted_password,
      plaintext_password: '',
    });
    // Initialize overrides from profile (backward compat: old profiles have all fields overridden)
    const initial = profile.overridden_fields
      ? new Set(profile.overridden_fields)
      : new Set(INHERITABLE_FIELDS as string[]);
    setOverrides(initial);
    // Fetch effective folder settings for this connection's folder
    (async () => {
      const eff = await apiGetEffectiveFolderSettings(profile.folder || '');
      setInherited(eff);
    })();
  }, [open, profile]);

  // Re-fetch inherited settings when folder changes
  const prevFolder = form.folder;
  useEffect(() => {
    if (!open) return;
    (async () => {
      const eff = await apiGetEffectiveFolderSettings(prevFolder || '');
      setInherited(eff);
    })();
  }, [open, prevFolder]);

  const update = useCallback(
    <K extends keyof ConnectionProfile>(key: K, value: ConnectionProfile[K]) => {
      setForm((prev) => ({ ...prev, [key]: value }));
    },
    []
  );

  const toggleOverride = useCallback((field: string, enable: boolean) => {
    setOverrides((prev) => {
      const next = new Set(prev);
      if (enable) {
        next.add(field);
      } else {
        next.delete(field);
      }
      return next;
    });
  }, []);

  const handleSave = () => {
    const profile = { ...form, overridden_fields: Array.from(overrides) };
    if (!profile.save_password) {
      profile.plaintext_password = '';
      profile.encrypted_password = '';
    }
    onSave(profile);
  };

  /** Render an inheritable boolean checkbox with override toggle */
  const inheritableCheckbox = (field: keyof FolderSettings & keyof ConnectionProfile, label: string) => {
    const isOverridden = overrides.has(field);
    const effectiveValue = isOverridden ? form[field] : (inherited[field] ?? false);
    return (
      <div className={styles.settingRow}>
        <Tooltip content={isOverridden ? 'Overridden — click to inherit from folder' : 'Inherited — click to override'} relationship="label">
          <Switch
            className={styles.setSwitch}
            checked={isOverridden}
            onChange={(_, d) => {
              toggleOverride(field, d.checked);
              if (d.checked) {
                // When starting to override, use the inherited value as starting point
                update(field, (inherited[field] ?? false) as ConnectionProfile[typeof field]);
              }
            }}
          />
        </Tooltip>
        <Checkbox
          label={label}
          checked={!!effectiveValue}
          disabled={!isOverridden}
          onChange={(_, d) => update(field, !!d.checked as ConnectionProfile[typeof field])}
        />
        {!isOverridden && inherited[field] !== undefined && (
          <Text className={styles.inheritedHint}>(inherited)</Text>
        )}
      </div>
    );
  };

  return (
    <Dialog open={open} onOpenChange={(_, data) => { if (!data.open) onCancel(); }}>
      <DialogSurface className={styles.surface}>
        <DialogBody>
          <DialogTitle
            action={
              <Button
                appearance="subtle"
                aria-label="close"
                icon={<Dismiss24Regular />}
                onClick={onCancel}
              />
            }
          >
            {isNew ? 'New Connection' : 'Edit Connection'}
          </DialogTitle>
          <DialogContent className={styles.content}>
            <div className={styles.form}>
              {/* Basic fields */}
              <div className={styles.field}>
                <Label required htmlFor="conn-name">Connection Name</Label>
                <Input
                  id="conn-name"
                  placeholder="My Server"
                  value={form.name}
                  onChange={(_, d) => update('name', d.value)}
                />
              </div>

              <div className={styles.field}>
                <Label htmlFor="conn-folder">Folder</Label>
                <Input
                  id="conn-folder"
                  placeholder="Example: Work/Production"
                  value={form.folder}
                  onChange={(_, d) => update('folder', d.value)}
                />
              </div>

              <div className={styles.row}>
                <div className={styles.field}>
                  <Label required htmlFor="conn-host">Hostname / IP</Label>
                  <Input
                    id="conn-host"
                    placeholder="server.domain.com"
                    value={form.hostname}
                    onChange={(_, d) => update('hostname', d.value)}
                  />
                </div>
                <div className={styles.fieldSmall}>
                  <Label htmlFor="conn-port">Port</Label>
                  <SpinButton
                    id="conn-port"
                    value={form.port}
                    min={1}
                    max={65535}
                    onChange={(_, d) => update('port', d.value ?? 3389)}
                  />
                </div>
              </div>

              <div className={styles.row}>
                <div className={styles.field}>
                  <Label htmlFor="conn-user">Username</Label>
                  <Input
                    id="conn-user"
                    placeholder="user@domain.com"
                    value={form.username}
                    onChange={(_, d) => update('username', d.value)}
                  />
                </div>
                <div className={styles.field}>
                  <Label htmlFor="conn-domain">Domain</Label>
                  <Input
                    id="conn-domain"
                    placeholder="DOMAIN"
                    value={form.domain}
                    onChange={(_, d) => update('domain', d.value)}
                  />
                </div>
              </div>

              <div className={styles.field}>
                <Label htmlFor="conn-password">Password</Label>
                <Input
                  id="conn-password"
                  type="password"
                  placeholder={form.encrypted_password ? '••••••••' : 'Optional'}
                  value={form.plaintext_password}
                  onChange={(_, d) => update('plaintext_password', d.value)}
                />
              </div>
              <Checkbox
                label="Save password (encrypted)"
                checked={form.save_password}
                onChange={(_, d) => update('save_password', d.checked === true)}
              />

              <Divider />

              <Accordion collapsible>
                {/* AVD / Dev Box Settings */}
                <AccordionItem value="avd">
                  <AccordionHeader>Azure Virtual Desktop / Dev Box Settings</AccordionHeader>
                  <AccordionPanel>
                    <div className={styles.form}>
                      <div className={styles.field}>
                        <Label htmlFor="conn-vm">Desktop/VM Name</Label>
                        <Input
                          id="conn-vm"
                          placeholder="My Dev Box"
                          value={form.remote_desktop_name}
                          onChange={(_, d) => update('remote_desktop_name', d.value)}
                        />
                      </div>
                      <div className={styles.field}>
                        <Label htmlFor="conn-pool">Pool ID (WVD Endpoint Pool)</Label>
                        <Input
                          id="conn-pool"
                          placeholder="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
                          value={form.wvd_endpoint_pool}
                          onChange={(_, d) => update('wvd_endpoint_pool', d.value)}
                        />
                      </div>
                      <div className={styles.field}>
                        <Label htmlFor="conn-ws">Workspace ID</Label>
                        <Input
                          id="conn-ws"
                          placeholder="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
                          value={form.workspace_id}
                          onChange={(_, d) => update('workspace_id', d.value)}
                        />
                      </div>
                      <div className={styles.field}>
                        <Label htmlFor="conn-arm">ARM Path</Label>
                        <Input
                          id="conn-arm"
                          placeholder="/subscriptions/.../providers/..."
                          value={form.arm_path}
                          onChange={(_, d) => update('arm_path', d.value)}
                        />
                      </div>
                    </div>
                  </AccordionPanel>
                </AccordionItem>

                {/* Advanced Options */}
                <AccordionItem value="advanced">
                  <AccordionHeader>Advanced Options</AccordionHeader>
                  <AccordionPanel>
                    <div className={styles.form}>
                      {/* Features */}
                      <div className={styles.group}>
                        <Text className={styles.groupTitle}>Features</Text>
                        {inheritableCheckbox('home_drive', 'Home Drive')}
                        {inheritableCheckbox('clipboard', 'Clipboard')}
                        {inheritableCheckbox('usb_auto', 'USB Auto')}
                        {inheritableCheckbox('floatbar', 'Float Bar')}
                      </div>

                      {/* Performance */}
                      <div className={styles.group}>
                        <Text className={styles.groupTitle}>Performance</Text>
                        {inheritableCheckbox('dynamic_resolution', 'Dynamic Resolution')}
                        {inheritableCheckbox('network_auto', 'Network Auto')}
                        {inheritableCheckbox('gfx_avc420', 'GFX AVC420')}
                        {inheritableCheckbox('compression', 'Compression')}
                      </div>

                      {/* Audio & Session */}
                      <div className={styles.group}>
                        <Text className={styles.groupTitle}>Audio & Session</Text>
                        {inheritableCheckbox('audio_pulse', 'PulseAudio')}
                        {inheritableCheckbox('prevent_session_lock', 'Prevent Lock')}
                      </div>

                      {/* Security & Reconnection */}
                      <div className={styles.group}>
                        <Text className={styles.groupTitle}>Security & Reconnection</Text>
                        {inheritableCheckbox('cert_tofu', 'Cert TOFU')}
                        {inheritableCheckbox('auto_reconnect', 'Auto Reconnect')}
                        {/* Max retries with override toggle */}
                        {(() => {
                          const isOverridden = overrides.has('auto_reconnect_max_retries');
                          const effectiveValue = isOverridden
                            ? form.auto_reconnect_max_retries
                            : (inherited.auto_reconnect_max_retries ?? 3);
                          return (
                            <div className={styles.settingRow}>
                              <Tooltip content={isOverridden ? 'Overridden — click to inherit' : 'Inherited — click to override'} relationship="label">
                                <Switch
                                  className={styles.setSwitch}
                                  checked={isOverridden}
                                  onChange={(_, d) => {
                                    toggleOverride('auto_reconnect_max_retries', d.checked);
                                    if (d.checked) {
                                      update('auto_reconnect_max_retries', inherited.auto_reconnect_max_retries ?? 3);
                                    }
                                  }}
                                />
                              </Tooltip>
                              <div className={styles.fieldSmall}>
                                <Label htmlFor="conn-retries">Max Retries</Label>
                                <SpinButton
                                  id="conn-retries"
                                  value={effectiveValue}
                                  min={1}
                                  max={10}
                                  disabled={!isOverridden}
                                  onChange={(_, d) =>
                                    update('auto_reconnect_max_retries', d.value ?? 3)
                                  }
                                />
                              </div>
                              {!isOverridden && inherited.auto_reconnect_max_retries !== undefined && (
                                <Text className={styles.inheritedHint}>(inherited)</Text>
                              )}
                            </div>
                          );
                        })()}
                      </div>

                      {/* Gateway / AVD */}
                      <div className={styles.group}>
                        <Text className={styles.groupTitle}>RD Gateway / Azure Virtual Desktop</Text>
                        {/* Gateway hostname with override toggle */}
                        {(() => {
                          const isOverridden = overrides.has('gateway_hostname');
                          return (
                            <div className={styles.settingRow}>
                              <Tooltip content={isOverridden ? 'Overridden — click to inherit' : 'Inherited — click to override'} relationship="label">
                                <Switch
                                  className={styles.setSwitch}
                                  checked={isOverridden}
                                  onChange={(_, d) => {
                                    toggleOverride('gateway_hostname', d.checked);
                                    if (d.checked) {
                                      update('gateway_hostname', inherited.gateway_hostname ?? '');
                                    }
                                  }}
                                />
                              </Tooltip>
                              <div className={styles.field}>
                                <Label htmlFor="conn-gw">Gateway Hostname</Label>
                                <Input
                                  id="conn-gw"
                                  placeholder="gateway.example.com:443"
                                  value={isOverridden ? form.gateway_hostname : (inherited.gateway_hostname ?? '')}
                                  disabled={!isOverridden}
                                  onChange={(_, d) => update('gateway_hostname', d.value)}
                                />
                              </div>
                              {!isOverridden && inherited.gateway_hostname !== undefined && (
                                <Text className={styles.inheritedHint}>(inherited)</Text>
                              )}
                            </div>
                          );
                        })()}
                        {inheritableCheckbox('enable_rds_aad_auth', 'AAD Auth')}
                        {inheritableCheckbox('target_is_aad_joined', 'AAD Joined')}
                        {inheritableCheckbox('use_manual_code_flow', 'Manual Code Flow')}
                        {/* Load balance info with override toggle */}
                        {(form.enable_rds_aad_auth || form.target_is_aad_joined ||
                          inherited.enable_rds_aad_auth || inherited.target_is_aad_joined) && (() => {
                          const isOverridden = overrides.has('load_balance_info');
                          return (
                            <div className={styles.settingRow}>
                              <Tooltip content={isOverridden ? 'Overridden — click to inherit' : 'Inherited — click to override'} relationship="label">
                                <Switch
                                  className={styles.setSwitch}
                                  checked={isOverridden}
                                  onChange={(_, d) => {
                                    toggleOverride('load_balance_info', d.checked);
                                    if (d.checked) {
                                      update('load_balance_info', inherited.load_balance_info ?? '');
                                    }
                                  }}
                                />
                              </Tooltip>
                              <div className={styles.field}>
                                <Label htmlFor="conn-lb">Load Balance Info (AVD)</Label>
                                <Input
                                  id="conn-lb"
                                  placeholder="mth://..."
                                  value={isOverridden ? form.load_balance_info : (inherited.load_balance_info ?? '')}
                                  disabled={!isOverridden}
                                  onChange={(_, d) => update('load_balance_info', d.value)}
                                />
                              </div>
                              {!isOverridden && inherited.load_balance_info !== undefined && (
                                <Text className={styles.inheritedHint}>(inherited)</Text>
                              )}
                            </div>
                          );
                        })()}
                      </div>
                    </div>
                  </AccordionPanel>
                </AccordionItem>
              </Accordion>
            </div>
          </DialogContent>
          <DialogActions>
            <Button appearance="secondary" onClick={onCancel}>
              Cancel
            </Button>
            <Button appearance="primary" onClick={handleSave}>
              Save
            </Button>
          </DialogActions>
        </DialogBody>
      </DialogSurface>
    </Dialog>
  );
}
