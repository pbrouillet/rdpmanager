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
  makeStyles,
  tokens,
  Accordion,
  AccordionItem,
  AccordionHeader,
  AccordionPanel,
  Divider,
  Text,
} from '@fluentui/react-components';
import { Dismiss24Regular } from '@fluentui/react-icons';
import type { ConnectionProfile } from '../types';

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

  useEffect(() => {
    if (open) setForm({ ...profile });
  }, [open, profile]);

  const update = useCallback(
    <K extends keyof ConnectionProfile>(key: K, value: ConnectionProfile[K]) => {
      setForm((prev) => ({ ...prev, [key]: value }));
    },
    []
  );

  const handleSave = () => onSave(form);

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
                        <div className={styles.checkboxGrid}>
                          <Checkbox
                            label="Home Drive"
                            checked={form.home_drive}
                            onChange={(_, d) => update('home_drive', !!d.checked)}
                          />
                          <Checkbox
                            label="Clipboard"
                            checked={form.clipboard}
                            onChange={(_, d) => update('clipboard', !!d.checked)}
                          />
                          <Checkbox
                            label="USB Auto"
                            checked={form.usb_auto}
                            onChange={(_, d) => update('usb_auto', !!d.checked)}
                          />
                          <Checkbox
                            label="Float Bar"
                            checked={form.floatbar}
                            onChange={(_, d) => update('floatbar', !!d.checked)}
                          />
                        </div>
                      </div>

                      {/* Performance */}
                      <div className={styles.group}>
                        <Text className={styles.groupTitle}>Performance</Text>
                        <div className={styles.checkboxGrid}>
                          <Checkbox
                            label="Dynamic Resolution"
                            checked={form.dynamic_resolution}
                            onChange={(_, d) => update('dynamic_resolution', !!d.checked)}
                          />
                          <Checkbox
                            label="Network Auto"
                            checked={form.network_auto}
                            onChange={(_, d) => update('network_auto', !!d.checked)}
                          />
                          <Checkbox
                            label="GFX AVC420"
                            checked={form.gfx_avc420}
                            onChange={(_, d) => update('gfx_avc420', !!d.checked)}
                          />
                          <Checkbox
                            label="Compression"
                            checked={form.compression}
                            onChange={(_, d) => update('compression', !!d.checked)}
                          />
                        </div>
                      </div>

                      {/* Audio & Session */}
                      <div className={styles.group}>
                        <Text className={styles.groupTitle}>Audio & Session</Text>
                        <div className={styles.checkboxGrid}>
                          <Checkbox
                            label="PulseAudio"
                            checked={form.audio_pulse}
                            onChange={(_, d) => update('audio_pulse', !!d.checked)}
                          />
                          <Checkbox
                            label="Prevent Lock"
                            checked={form.prevent_session_lock}
                            onChange={(_, d) => update('prevent_session_lock', !!d.checked)}
                          />
                        </div>
                      </div>

                      {/* Security & Reconnection */}
                      <div className={styles.group}>
                        <Text className={styles.groupTitle}>Security & Reconnection</Text>
                        <div className={styles.checkboxGrid}>
                          <Checkbox
                            label="Cert TOFU"
                            checked={form.cert_tofu}
                            onChange={(_, d) => update('cert_tofu', !!d.checked)}
                          />
                          <Checkbox
                            label="Auto Reconnect"
                            checked={form.auto_reconnect}
                            onChange={(_, d) => update('auto_reconnect', !!d.checked)}
                          />
                        </div>
                        {form.auto_reconnect && (
                          <div className={styles.row} style={{ marginTop: tokens.spacingVerticalS }}>
                            <div className={styles.fieldSmall}>
                              <Label htmlFor="conn-retries">Max Retries</Label>
                              <SpinButton
                                id="conn-retries"
                                value={form.auto_reconnect_max_retries}
                                min={1}
                                max={10}
                                onChange={(_, d) =>
                                  update('auto_reconnect_max_retries', d.value ?? 3)
                                }
                              />
                            </div>
                          </div>
                        )}
                      </div>

                      {/* Gateway / AVD */}
                      <div className={styles.group}>
                        <Text className={styles.groupTitle}>RD Gateway / Azure Virtual Desktop</Text>
                        <div className={styles.field}>
                          <Label htmlFor="conn-gw">Gateway Hostname</Label>
                          <Input
                            id="conn-gw"
                            placeholder="gateway.example.com:443"
                            value={form.gateway_hostname}
                            onChange={(_, d) => update('gateway_hostname', d.value)}
                          />
                        </div>
                        <div className={styles.checkboxGrid} style={{ marginTop: tokens.spacingVerticalS }}>
                          <Checkbox
                            label="AAD Auth"
                            checked={form.enable_rds_aad_auth}
                            onChange={(_, d) => update('enable_rds_aad_auth', !!d.checked)}
                          />
                          <Checkbox
                            label="AAD Joined"
                            checked={form.target_is_aad_joined}
                            onChange={(_, d) => update('target_is_aad_joined', !!d.checked)}
                          />
                        </div>
                        {(form.enable_rds_aad_auth || form.target_is_aad_joined) && (
                          <div className={styles.field} style={{ marginTop: tokens.spacingVerticalS }}>
                            <Label htmlFor="conn-lb">Load Balance Info (AVD)</Label>
                            <Input
                              id="conn-lb"
                              placeholder="mth://..."
                              value={form.load_balance_info}
                              onChange={(_, d) => update('load_balance_info', d.value)}
                            />
                          </div>
                        )}
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
