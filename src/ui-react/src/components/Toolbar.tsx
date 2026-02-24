import { useRef } from 'react';
import {
  makeStyles,
  tokens,
  Button,
} from '@fluentui/react-components';
import { Add24Regular, ArrowUpload24Regular } from '@fluentui/react-icons';

const useStyles = makeStyles({
  toolbar: {
    display: 'flex',
    gap: tokens.spacingHorizontalM,
    padding: tokens.spacingVerticalM,
    backgroundColor: tokens.colorNeutralBackground2,
    borderRadius: tokens.borderRadiusLarge,
    border: `1px solid ${tokens.colorNeutralStroke1}`,
  },
});

interface ToolbarProps {
  onNewConnection: () => void;
  onImportFile: (file: File) => void;
}

export function Toolbar({ onNewConnection, onImportFile }: ToolbarProps) {
  const styles = useStyles();
  const fileInputRef = useRef<HTMLInputElement>(null);

  const handleImportClick = () => {
    fileInputRef.current?.click();
  };

  const handleFileChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (file) {
      onImportFile(file);
      e.target.value = '';
    }
  };

  return (
    <div className={styles.toolbar}>
      <Button
        appearance="primary"
        icon={<Add24Regular />}
        onClick={onNewConnection}
      >
        New Connection
      </Button>
      <input
        ref={fileInputRef}
        type="file"
        accept=".rdp,.rdpw"
        style={{ display: 'none' }}
        onChange={handleFileChange}
      />
      <Button
        appearance="secondary"
        icon={<ArrowUpload24Regular />}
        onClick={handleImportClick}
      >
        Import
      </Button>
    </div>
  );
}
