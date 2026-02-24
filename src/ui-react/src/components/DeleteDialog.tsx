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
import { Warning24Regular, Dismiss24Regular } from '@fluentui/react-icons';

interface DeleteDialogProps {
  open: boolean;
  connectionName: string;
  onConfirm: () => void;
  onCancel: () => void;
}

export function DeleteDialog({ open, connectionName, onConfirm, onCancel }: DeleteDialogProps) {
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
            <span style={{ display: 'flex', alignItems: 'center', gap: '8px', color: tokens.colorPaletteRedForeground1 }}>
              <Warning24Regular />
              Delete Connection
            </span>
          </DialogTitle>
          <DialogContent>
            <Text block>Are you sure you want to delete this connection?</Text>
            <Text
              block
              weight="semibold"
              style={{ marginTop: tokens.spacingVerticalM }}
            >
              {connectionName}
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
              style={{ backgroundColor: tokens.colorPaletteRedBackground3, borderColor: tokens.colorPaletteRedBackground3 }}
              onClick={onConfirm}
            >
              Delete
            </Button>
          </DialogActions>
        </DialogBody>
      </DialogSurface>
    </Dialog>
  );
}
