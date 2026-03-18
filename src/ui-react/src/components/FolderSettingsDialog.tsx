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
  Text,
  Tooltip,
} from '@fluentui/react-components';
import { Dismiss24Regular } from '@fluentui/react-icons';
import type { FolderSettings } from '../types';
import { apiGetFolderSettings, apiGetEffectiveFolderSettings } from '../api';

const useStyles = makeStyles({
  form: {
    display: 'flex',
    flexDirection: 'column',
    gap: tokens.spacingVerticalM,
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
    width: 'min(620px, calc(100vw - 24px))',
    maxWidth: '620px',
    maxHeight: 'calc(100vh - 24px)',
  },
  content: {
    overflowY: 'auto',
    maxHeight: 'calc(100vh - 220px)',
    minHeight: '180px',
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
  field: {
    flex: 1,
    display: 'flex',
    flexDirection: 'column',
    gap: tokens.spacingVerticalXS,
  },
  row: {
    display: 'flex',
    flexWrap: 'wrap',
    gap: tokens.spacingHorizontalM,
  },
  fieldSmall: {
    flex: '0 0 120px',
    minWidth: '120px',
    display: 'flex',
    flexDirection: 'column',
    gap: tokens.spacingVerticalXS,
  },
});

interface FolderSettingsDialogProps {
  open: boolean;
  folderPath: string;
  onSave: (path: string, settings: FolderSettings) => void;
  onCancel: () => void;
}

/** Get the parent folder path (empty for root-level folders) */
function parentPath(path: string): string {
  const idx = path.lastIndexOf('/');
  return idx === -1 ? '' : path.substring(0, idx);
}

export function FolderSettingsDialog({
  open,
  folderPath,
  onSave,
  onCancel,
}: FolderSettingsDialogProps) {
  const styles = useStyles();
  const [settings, setSettings] = useState<FolderSettings>({});
  const [inherited, setInherited] = useState<FolderSettings>({});

  useEffect(() => {
    if (!open) return;
    // Load this folder's own settings and the effective inherited settings from parent
    (async () => {
      const [own, parentEffective] = await Promise.all([
        apiGetFolderSettings(folderPath),
        apiGetEffectiveFolderSettings(parentPath(folderPath)),
      ]);
      setSettings(own);
      setInherited(parentEffective);
    })();
  }, [open, folderPath]);

  const setField = useCallback(
    <K extends keyof FolderSettings>(key: K, value: FolderSettings[K]) => {
      setSettings((prev) => ({ ...prev, [key]: value }));
    },
    []
  );

  const clearField = useCallback((key: keyof FolderSettings) => {
    setSettings((prev) => {
      const next = { ...prev };
      delete next[key];
      return next;
    });
  }, []);

  const handleSave = () => onSave(folderPath, settings);

  /** Render a boolean setting with "set at this level" toggle */
  const boolSetting = (key: keyof FolderSettings, label: string) => {
    const isSet = settings[key] !== undefined;
    const effectiveValue = isSet ? settings[key] : inherited[key];
    return (
      <div className={styles.settingRow}>
        <Tooltip content={isSet ? 'Set at this level — click to inherit' : 'Inherited — click to override'} relationship="label">
          <Switch
            className={styles.setSwitch}
            checked={isSet}
            onChange={(_, d) => {
              if (d.checked) {
                // Start overriding with inherited value or false
                setField(key, (inherited[key] ?? false) as FolderSettings[typeof key]);
              } else {
                clearField(key);
              }
            }}
          />
        </Tooltip>
        <Checkbox
          label={label}
          checked={!!effectiveValue}
          disabled={!isSet}
          onChange={(_, d) => setField(key, !!d.checked as FolderSettings[typeof key])}
        />
        {!isSet && effectiveValue !== undefined && (
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
            Folder Defaults — {folderPath || 'Root'}
          </DialogTitle>
          <DialogContent className={styles.content}>
            <Text style={{ marginBottom: tokens.spacingVerticalM, display: 'block' }}>
              Settings defined here cascade to all subfolders and connections.
              Toggle the switch to set a value at this level; leave off to inherit from the parent.
            </Text>
            <div className={styles.form}>
              {/* Features */}
              <div className={styles.group}>
                <Text className={styles.groupTitle}>Features</Text>
                {boolSetting('home_drive', 'Home Drive')}
                {boolSetting('clipboard', 'Clipboard')}
                {boolSetting('usb_auto', 'USB Auto')}
                {boolSetting('floatbar', 'Float Bar')}
              </div>

              {/* Performance */}
              <div className={styles.group}>
                <Text className={styles.groupTitle}>Performance</Text>
                {boolSetting('dynamic_resolution', 'Dynamic Resolution')}
                {boolSetting('network_auto', 'Network Auto')}
                {boolSetting('gfx_avc420', 'GFX AVC420')}
                {boolSetting('compression', 'Compression')}
              </div>

              {/* Audio & Session */}
              <div className={styles.group}>
                <Text className={styles.groupTitle}>Audio & Session</Text>
                {boolSetting('audio_pulse', 'PulseAudio')}
                {boolSetting('prevent_session_lock', 'Prevent Lock')}
              </div>

              {/* Security & Reconnection */}
              <div className={styles.group}>
                <Text className={styles.groupTitle}>Security & Reconnection</Text>
                {boolSetting('cert_tofu', 'Cert TOFU')}
                {boolSetting('auto_reconnect', 'Auto Reconnect')}
                {/* Auto reconnect max retries */}
                <div className={styles.settingRow}>
                  <Tooltip
                    content={
                      settings.auto_reconnect_max_retries !== undefined
                        ? 'Set at this level — click to inherit'
                        : 'Inherited — click to override'
                    }
                    relationship="label"
                  >
                    <Switch
                      className={styles.setSwitch}
                      checked={settings.auto_reconnect_max_retries !== undefined}
                      onChange={(_, d) => {
                        if (d.checked) {
                          setField('auto_reconnect_max_retries', inherited.auto_reconnect_max_retries ?? 3);
                        } else {
                          clearField('auto_reconnect_max_retries');
                        }
                      }}
                    />
                  </Tooltip>
                  <div className={styles.fieldSmall}>
                    <Label htmlFor="fs-retries">Max Retries</Label>
                    <SpinButton
                      id="fs-retries"
                      value={settings.auto_reconnect_max_retries ?? inherited.auto_reconnect_max_retries ?? 3}
                      min={1}
                      max={10}
                      disabled={settings.auto_reconnect_max_retries === undefined}
                      onChange={(_, d) => setField('auto_reconnect_max_retries', d.value ?? 3)}
                    />
                  </div>
                  {settings.auto_reconnect_max_retries === undefined &&
                    inherited.auto_reconnect_max_retries !== undefined && (
                      <Text className={styles.inheritedHint}>(inherited)</Text>
                    )}
                </div>
              </div>

              {/* Gateway / AVD */}
              <div className={styles.group}>
                <Text className={styles.groupTitle}>RD Gateway / Azure Virtual Desktop</Text>
                {/* Gateway hostname */}
                <div className={styles.settingRow}>
                  <Tooltip
                    content={
                      settings.gateway_hostname !== undefined
                        ? 'Set at this level — click to inherit'
                        : 'Inherited — click to override'
                    }
                    relationship="label"
                  >
                    <Switch
                      className={styles.setSwitch}
                      checked={settings.gateway_hostname !== undefined}
                      onChange={(_, d) => {
                        if (d.checked) {
                          setField('gateway_hostname', inherited.gateway_hostname ?? '');
                        } else {
                          clearField('gateway_hostname');
                        }
                      }}
                    />
                  </Tooltip>
                  <div className={styles.field}>
                    <Label htmlFor="fs-gw">Gateway Hostname</Label>
                    <Input
                      id="fs-gw"
                      placeholder="gateway.example.com:443"
                      value={settings.gateway_hostname ?? inherited.gateway_hostname ?? ''}
                      disabled={settings.gateway_hostname === undefined}
                      onChange={(_, d) => setField('gateway_hostname', d.value)}
                    />
                  </div>
                  {settings.gateway_hostname === undefined &&
                    inherited.gateway_hostname !== undefined && (
                      <Text className={styles.inheritedHint}>(inherited)</Text>
                    )}
                </div>
                {boolSetting('enable_rds_aad_auth', 'AAD Auth')}
                {boolSetting('target_is_aad_joined', 'AAD Joined')}
                {/* Load balance info */}
                <div className={styles.settingRow}>
                  <Tooltip
                    content={
                      settings.load_balance_info !== undefined
                        ? 'Set at this level — click to inherit'
                        : 'Inherited — click to override'
                    }
                    relationship="label"
                  >
                    <Switch
                      className={styles.setSwitch}
                      checked={settings.load_balance_info !== undefined}
                      onChange={(_, d) => {
                        if (d.checked) {
                          setField('load_balance_info', inherited.load_balance_info ?? '');
                        } else {
                          clearField('load_balance_info');
                        }
                      }}
                    />
                  </Tooltip>
                  <div className={styles.field}>
                    <Label htmlFor="fs-lb">Load Balance Info</Label>
                    <Input
                      id="fs-lb"
                      placeholder="mth://..."
                      value={settings.load_balance_info ?? inherited.load_balance_info ?? ''}
                      disabled={settings.load_balance_info === undefined}
                      onChange={(_, d) => setField('load_balance_info', d.value)}
                    />
                  </div>
                  {settings.load_balance_info === undefined &&
                    inherited.load_balance_info !== undefined && (
                      <Text className={styles.inheritedHint}>(inherited)</Text>
                    )}
                </div>
              </div>
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
