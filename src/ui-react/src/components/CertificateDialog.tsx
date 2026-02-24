import {
  Dialog,
  DialogSurface,
  DialogBody,
  DialogTitle,
  DialogContent,
  DialogActions,
  Button,
  Text,
  makeStyles,
  tokens,
  MessageBar,
  MessageBarBody,
} from '@fluentui/react-components';
import { Warning24Regular } from '@fluentui/react-icons';
import type { CertificateInfo } from '../types';

const useStyles = makeStyles({
  surface: {
    maxWidth: '550px',
  },
  details: {
    backgroundColor: tokens.colorNeutralBackground3,
    borderRadius: tokens.borderRadiusMedium,
    padding: tokens.spacingVerticalM,
    marginTop: tokens.spacingVerticalM,
  },
  row: {
    display: 'flex',
    gap: tokens.spacingHorizontalM,
    padding: `${tokens.spacingVerticalXS} 0`,
    borderBottom: `1px solid ${tokens.colorNeutralStroke1}`,
    ':last-child': {
      borderBottom: 'none',
    },
  },
  label: {
    color: tokens.colorNeutralForeground3,
    minWidth: '130px',
    flexShrink: 0,
  },
  value: {
    wordBreak: 'break-all' as const,
  },
  fingerprint: {
    fontFamily: tokens.fontFamilyMonospace,
    fontSize: tokens.fontSizeBase200,
    color: tokens.colorBrandForeground1,
  },
});

interface CertificateDialogProps {
  open: boolean;
  certInfo: CertificateInfo | null;
  onReject: () => void;
  onAcceptTemp: () => void;
  onAcceptPerm: () => void;
}

export function CertificateDialog({
  open,
  certInfo,
  onReject,
  onAcceptTemp,
  onAcceptPerm,
}: CertificateDialogProps) {
  const styles = useStyles();

  if (!certInfo) return null;

  return (
    <Dialog open={open}>
      <DialogSurface className={styles.surface}>
        <DialogBody>
          <DialogTitle>
            <span style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
              <Warning24Regular style={{ color: tokens.colorPaletteRedForeground1 }} />
              {certInfo.isChanged
                ? 'Certificate Has Changed!'
                : 'Certificate Verification Required'}
            </span>
          </DialogTitle>
          <DialogContent>
            <MessageBar intent="warning">
              <MessageBarBody>
                {certInfo.isChanged ? (
                  <>
                    <strong>WARNING:</strong> The server's certificate has changed since your last
                    connection. This could indicate a man-in-the-middle attack, or the server may
                    have simply updated its certificate.
                  </>
                ) : (
                  <>
                    The server's certificate could not be verified. This may indicate a self-signed
                    certificate, an untrusted authority, or a potential security issue.
                  </>
                )}
              </MessageBarBody>
            </MessageBar>

            <div className={styles.details}>
              <div className={styles.row}>
                <Text className={styles.label}>Host:</Text>
                <Text className={styles.value}>
                  {certInfo.host}:{certInfo.port}
                </Text>
              </div>
              <div className={styles.row}>
                <Text className={styles.label}>Common Name:</Text>
                <Text className={styles.value}>{certInfo.commonName || '-'}</Text>
              </div>
              <div className={styles.row}>
                <Text className={styles.label}>Subject:</Text>
                <Text className={styles.value}>{certInfo.subject || '-'}</Text>
              </div>
              <div className={styles.row}>
                <Text className={styles.label}>Issuer:</Text>
                <Text className={styles.value}>{certInfo.issuer || '-'}</Text>
              </div>
              <div className={styles.row}>
                <Text className={styles.label}>Fingerprint:</Text>
                <Text className={`${styles.value} ${styles.fingerprint}`}>
                  {certInfo.fingerprint || '-'}
                </Text>
              </div>
              {certInfo.isChanged && certInfo.oldFingerprint && (
                <div className={styles.row}>
                  <Text className={styles.label}>Previous Fingerprint:</Text>
                  <Text className={`${styles.value} ${styles.fingerprint}`}>
                    {certInfo.oldFingerprint}
                  </Text>
                </div>
              )}
            </div>
          </DialogContent>
          <DialogActions>
            <Button
              appearance="secondary"
              style={{ color: tokens.colorPaletteRedForeground1 }}
              onClick={onReject}
            >
              Reject
            </Button>
            <Button appearance="secondary" onClick={onAcceptTemp}>
              Accept (This Session)
            </Button>
            <Button appearance="primary" onClick={onAcceptPerm}>
              Accept & Save
            </Button>
          </DialogActions>
        </DialogBody>
      </DialogSurface>
    </Dialog>
  );
}
