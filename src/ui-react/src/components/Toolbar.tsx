import { useRef } from 'react';
import {
  makeStyles,
  tokens,
  Button,
  Menu,
  MenuTrigger,
  MenuPopover,
  MenuList,
  MenuItem,
} from '@fluentui/react-components';
import { Add24Regular, ArrowUpload24Regular, Database24Regular } from '@fluentui/react-icons';

const useStyles = makeStyles({
  toolbar: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalS,
    padding: `${tokens.spacingVerticalXS} ${tokens.spacingHorizontalM}`,
    backgroundColor: tokens.colorNeutralBackground2,
    borderBottom: `1px solid ${tokens.colorNeutralStroke2}`,
  },
});

interface ToolbarProps {
  onNewConnection: () => void;
  onImportFile: (file: File) => void;
  onCreateDatabase: () => void;
  onOpenDatabase: () => void;
  onCloseDatabase: () => void;
  databaseOpen: boolean;
}

export function Toolbar({
  onNewConnection,
  onImportFile,
  onCreateDatabase,
  onOpenDatabase,
  onCloseDatabase,
  databaseOpen,
}: ToolbarProps) {
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
      <Menu>
        <MenuTrigger disableButtonEnhancement>
          <Button appearance="secondary" icon={<Database24Regular />}>
            Database
          </Button>
        </MenuTrigger>
        <MenuPopover>
          <MenuList>
            <MenuItem onClick={onCreateDatabase}>Create Database</MenuItem>
            <MenuItem onClick={onOpenDatabase}>Open Database</MenuItem>
            <MenuItem disabled={!databaseOpen} onClick={onCloseDatabase}>Close Database</MenuItem>
          </MenuList>
        </MenuPopover>
      </Menu>
    </div>
  );
}
