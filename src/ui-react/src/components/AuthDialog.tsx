import { useState, useEffect } from 'react';
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
  makeStyles,
  tokens,
  Text,
} from '@fluentui/react-components';
import { Dismiss24Regular, LockClosed24Regular } from '@fluentui/react-icons';
import type { AuthRequest } from '../types';

const useStyles = makeStyles({
  form: {
    display: 'flex',
    flexDirection: 'column',
    gap: tokens.spacingVerticalM,
  },
  field: {
    display: 'flex',
    flexDirection: 'column',
    gap: tokens.spacingVerticalXS,
  },
});

interface AuthDialogProps {
  open: boolean;
  authRequest: AuthRequest | null;
  onSubmit: (username: string, password: string, domain: string) => void;
  onCancel: () => void;
}

export function AuthDialog({ open, authRequest, onSubmit, onCancel }: AuthDialogProps) {
  const styles = useStyles();
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [domain, setDomain] = useState('');

  useEffect(() => {
    if (open && authRequest) {
      setUsername(authRequest.currentUsername || '');
      setDomain(authRequest.currentDomain || '');
      setPassword('');
    }
  }, [open, authRequest]);

  if (!authRequest) return null;

  const title = authRequest.isGateway
    ? 'Gateway Authentication Required'
    : 'Authentication Required';
  const prompt = authRequest.isGateway
    ? `Enter your credentials for gateway: ${authRequest.hostname}`
    : `Enter your credentials for: ${authRequest.hostname}`;

  const handleSubmit = () => onSubmit(username, password, domain);

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      handleSubmit();
    }
  };

  return (
    <Dialog open={open} onOpenChange={(_, data) => { if (!data.open) onCancel(); }}>
      <DialogSurface>
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
            <span style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
              <LockClosed24Regular />
              {title}
            </span>
          </DialogTitle>
          <DialogContent>
            <Text style={{ marginBottom: tokens.spacingVerticalM, display: 'block' }}>
              {prompt}
            </Text>
            <div className={styles.form}>
              <div className={styles.field}>
                <Label htmlFor="auth-user">Username</Label>
                <Input
                  id="auth-user"
                  placeholder="user@domain.com"
                  value={username}
                  onChange={(_, d) => setUsername(d.value)}
                />
              </div>
              <div className={styles.field}>
                <Label htmlFor="auth-pass">Password</Label>
                <Input
                  id="auth-pass"
                  type="password"
                  placeholder="Password"
                  value={password}
                  onChange={(_, d) => setPassword(d.value)}
                  onKeyDown={handleKeyDown}
                />
              </div>
              <div className={styles.field}>
                <Label htmlFor="auth-domain">Domain</Label>
                <Input
                  id="auth-domain"
                  placeholder="DOMAIN"
                  value={domain}
                  onChange={(_, d) => setDomain(d.value)}
                />
              </div>
            </div>
          </DialogContent>
          <DialogActions>
            <Button appearance="secondary" onClick={onCancel}>
              Cancel
            </Button>
            <Button appearance="primary" onClick={handleSubmit}>
              Connect
            </Button>
          </DialogActions>
        </DialogBody>
      </DialogSurface>
    </Dialog>
  );
}
