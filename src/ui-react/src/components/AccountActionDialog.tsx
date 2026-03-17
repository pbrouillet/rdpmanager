import {
  Dialog,
  DialogSurface,
  DialogBody,
  DialogTitle,
  DialogContent,
  DialogActions,
  Button,
  Text,
  tokens,
} from '@fluentui/react-components';
import { Warning24Regular, Dismiss24Regular, SignOut24Regular, Delete24Regular } from '@fluentui/react-icons';

export type AccountAction = 'logoff' | 'forget';

interface AccountActionDialogProps {
  open: boolean;
  action: AccountAction;
  accountName: string;
  onConfirm: () => void;
  onCancel: () => void;
}

const config = {
  logoff: {
    title: 'Log Off Account',
    icon: <SignOut24Regular />,
    description: 'This will clear all cached authentication tokens for this account. You will need to re-authenticate on the next connection.',
    confirmLabel: 'Log Off',
    titleColor: tokens.colorPaletteMarigoldForeground1,
    buttonBg: tokens.colorPaletteMarigoldBackground3,
    buttonBorder: tokens.colorPaletteMarigoldBackground3,
  },
  forget: {
    title: 'Forget Account',
    icon: <Delete24Regular />,
    description: 'This will permanently remove the account, all its imported connections, and cached tokens.',
    confirmLabel: 'Forget Account',
    titleColor: tokens.colorPaletteRedForeground1,
    buttonBg: tokens.colorPaletteRedBackground3,
    buttonBorder: tokens.colorPaletteRedBackground3,
  },
};

export function AccountActionDialog({ open, action, accountName, onConfirm, onCancel }: AccountActionDialogProps) {
  const c = config[action];

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
            <span style={{ display: 'flex', alignItems: 'center', gap: '8px', color: c.titleColor }}>
              <Warning24Regular />
              {c.title}
            </span>
          </DialogTitle>
          <DialogContent>
            <Text block>{c.description}</Text>
            <Text
              block
              weight="semibold"
              style={{ marginTop: tokens.spacingVerticalM }}
            >
              {accountName}
            </Text>
            <Text
              block
              size={200}
              style={{ marginTop: tokens.spacingVerticalS, color: tokens.colorNeutralForeground4 }}
            >
              This action cannot be undone.
            </Text>
          </DialogContent>
          <DialogActions>
            <Button appearance="secondary" onClick={onCancel}>
              Cancel
            </Button>
            <Button
              appearance="primary"
              style={{ backgroundColor: c.buttonBg, borderColor: c.buttonBorder }}
              onClick={onConfirm}
            >
              {c.confirmLabel}
            </Button>
          </DialogActions>
        </DialogBody>
      </DialogSurface>
    </Dialog>
  );
}
