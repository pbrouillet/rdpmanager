import { useState, useCallback } from 'react';
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
  Textarea,
  makeStyles,
  tokens,
  Text,
  Badge,
} from '@fluentui/react-components';
import { Copy24Regular, Dismiss24Regular } from '@fluentui/react-icons';

export interface ManualCodeFlowInfo {
  auth_url: string;
  type: string;        // "RDS_AAD" | "AVD"
  step_current: number;
  step_total: number;
  step_label: string;
  redirect_uri: string;
}

interface ManualCodeFlowDialogProps {
  open: boolean;
  info: ManualCodeFlowInfo | null;
  onSubmit: (redirectUrl: string) => void;
  onCancel: () => void;
}

const useStyles = makeStyles({
  surface: {
    width: 'min(640px, calc(100vw - 24px))',
    maxWidth: '640px',
  },
  content: {
    display: 'flex',
    flexDirection: 'column',
    gap: tokens.spacingVerticalM,
  },
  stepBadge: {
    marginLeft: tokens.spacingHorizontalS,
  },
  urlRow: {
    display: 'flex',
    alignItems: 'flex-start',
    gap: tokens.spacingHorizontalS,
  },
  urlInput: {
    flex: 1,
  },
  instructions: {
    color: tokens.colorNeutralForeground3,
    fontSize: tokens.fontSizeBase200,
    lineHeight: tokens.lineHeightBase200,
  },
  field: {
    display: 'flex',
    flexDirection: 'column',
    gap: tokens.spacingVerticalXS,
  },
});

export function ManualCodeFlowDialog({ open, info, onSubmit, onCancel }: ManualCodeFlowDialogProps) {
  const styles = useStyles();
  const [redirectUrl, setRedirectUrl] = useState('');
  const [copied, setCopied] = useState(false);

  const handleCopy = useCallback(async () => {
    if (!info) return;
    try {
      await navigator.clipboard.writeText(info.auth_url);
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    } catch {
      // Fallback: select text for manual copy
    }
  }, [info]);

  const handleSubmit = useCallback(() => {
    const trimmed = redirectUrl.trim();
    if (trimmed) {
      onSubmit(trimmed);
      setRedirectUrl('');
    }
  }, [redirectUrl, onSubmit]);

  const handleCancel = useCallback(() => {
    setRedirectUrl('');
    onCancel();
  }, [onCancel]);

  if (!info) return null;

  const title = info.step_total > 1
    ? `Azure AD Authentication — Step ${info.step_current} of ${info.step_total}: ${info.step_label}`
    : 'Azure AD Authentication';

  return (
    <Dialog open={open} modalType="alert">
      <DialogSurface className={styles.surface}>
        <DialogBody>
          <DialogTitle
            action={
              <Button appearance="subtle" aria-label="close" icon={<Dismiss24Regular />} onClick={handleCancel} />
            }
          >
            {title}
            {info.step_total > 1 && (
              <Badge className={styles.stepBadge} appearance="outline" color="informative">
                {info.type}
              </Badge>
            )}
          </DialogTitle>
          <DialogContent className={styles.content}>
            <div className={styles.field}>
              <Label htmlFor="mcf-url">Authentication URL</Label>
              <div className={styles.urlRow}>
                <Input
                  id="mcf-url"
                  className={styles.urlInput}
                  readOnly
                  value={info.auth_url}
                />
                <Button
                  appearance="secondary"
                  icon={<Copy24Regular />}
                  onClick={handleCopy}
                >
                  {copied ? 'Copied!' : 'Copy'}
                </Button>
              </div>
            </div>

            <Text className={styles.instructions}>
              1. Copy the URL above and open it in your browser.{'\n'}
              2. Sign in with your Azure AD credentials.{'\n'}
              3. After authentication, you will be redirected to a URL starting
              with <strong>{info.redirect_uri || 'the redirect URI'}</strong>.{'\n'}
              4. Copy the <strong>entire</strong> redirect URL (including the ?code=… part) and paste it below.
            </Text>

            <div className={styles.field}>
              <Label htmlFor="mcf-redirect">Redirect URL</Label>
              <Textarea
                id="mcf-redirect"
                placeholder="Paste the full redirect URL here…"
                value={redirectUrl}
                onChange={(_, d) => setRedirectUrl(d.value)}
                rows={3}
              />
            </div>
          </DialogContent>
          <DialogActions>
            <Button appearance="secondary" onClick={handleCancel}>
              Cancel
            </Button>
            <Button appearance="primary" onClick={handleSubmit} disabled={!redirectUrl.trim()}>
              Submit
            </Button>
          </DialogActions>
        </DialogBody>
      </DialogSurface>
    </Dialog>
  );
}
