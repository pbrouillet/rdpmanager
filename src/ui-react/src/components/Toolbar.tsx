import { useRef } from 'react';
import {
  makeStyles,
  tokens,
  Button,
  ToggleButton,
  Menu,
  MenuTrigger,
  MenuPopover,
  MenuList,
  MenuItem,
  MenuDivider,
  Spinner,
  Text,
  Tooltip,
} from '@fluentui/react-components';
import {
  Add24Regular,
  ArrowUpload24Regular,
  Database24Regular,
  People24Regular,
  ArrowSync24Regular,
  Delete24Regular,
  PersonAdd24Regular,
  SignOut24Regular,
  Grid24Regular,
  TextBulletListLtr24Regular,
} from '@fluentui/react-icons';
import type { FeedAccount, ViewMode } from '../types';

const useStyles = makeStyles({
  toolbar: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalS,
    padding: `${tokens.spacingVerticalXS} ${tokens.spacingHorizontalM}`,
    backgroundColor: tokens.colorNeutralBackground2,
    borderBottom: `1px solid ${tokens.colorNeutralStroke2}`,
  },
  accountItem: {
    display: 'flex',
    flexDirection: 'column',
    gap: '2px',
  },
  accountEmail: {
    fontSize: tokens.fontSizeBase100,
    color: tokens.colorNeutralForeground4,
  },
  viewToggle: {
    marginLeft: 'auto',
    display: 'flex',
    gap: '2px',
  },
});

interface ToolbarProps {
  onNewConnection: () => void;
  onImportFile: (file: File) => void;
  onCreateDatabase: () => void;
  onOpenDatabase: () => void;
  onCloseDatabase: () => void;
  databaseOpen: boolean;
  // Feed discovery
  feedAccounts: FeedAccount[];
  onAddAccount: () => void;
  onLogOffAccount: (id: string) => void;
  onForgetAccount: (id: string) => void;
  onDiscoverFeeds: (account: FeedAccount) => void;
  discoveryInProgress: boolean;
  viewMode: ViewMode;
  onViewModeChange: (mode: ViewMode) => void;
}

export function Toolbar({
  onNewConnection,
  onImportFile,
  onCreateDatabase,
  onOpenDatabase,
  onCloseDatabase,
  databaseOpen,
  feedAccounts,
  onAddAccount,
  onLogOffAccount,
  onForgetAccount,
  onDiscoverFeeds,
  discoveryInProgress,
  viewMode,
  onViewModeChange,
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
      <Menu>
        <MenuTrigger disableButtonEnhancement>
          <Button
            appearance="secondary"
            icon={discoveryInProgress ? <Spinner size="tiny" /> : <People24Regular />}
            disabled={!databaseOpen || discoveryInProgress}
          >
            Accounts
          </Button>
        </MenuTrigger>
        <MenuPopover>
          <MenuList>
            {feedAccounts.map((account) => (
              <Menu key={account.id}>
                <MenuTrigger disableButtonEnhancement>
                  <MenuItem>
                    <div className={styles.accountItem}>
                      <Text weight="semibold">{account.display_name}</Text>
                      {account.last_synced > 0 && (
                        <Text className={styles.accountEmail}>
                          Last synced: {new Date(account.last_synced * 1000).toLocaleString()}
                        </Text>
                      )}
                    </div>
                  </MenuItem>
                </MenuTrigger>
                <MenuPopover>
                  <MenuList>
                    <MenuItem
                      icon={<ArrowSync24Regular />}
                      onClick={() => onDiscoverFeeds(account)}
                      disabled={discoveryInProgress}
                    >
                      Refresh Feeds
                    </MenuItem>
                    <MenuDivider />
                    <MenuItem
                      icon={<SignOut24Regular />}
                      onClick={() => onLogOffAccount(account.id)}
                    >
                      Log Off
                    </MenuItem>
                    <MenuItem
                      icon={<Delete24Regular />}
                      onClick={() => onForgetAccount(account.id)}
                    >
                      Forget Account
                    </MenuItem>
                  </MenuList>
                </MenuPopover>
              </Menu>
            ))}
            {feedAccounts.length > 0 && <MenuDivider />}
            <MenuItem
              icon={<PersonAdd24Regular />}
              onClick={onAddAccount}
              disabled={discoveryInProgress}
            >
              Add Account
            </MenuItem>
          </MenuList>
        </MenuPopover>
      </Menu>
      <div className={styles.viewToggle}>
        <Tooltip content="Grid view" relationship="label">
          <ToggleButton
            appearance="subtle"
            icon={<Grid24Regular />}
            checked={viewMode === 'grid'}
            onClick={() => onViewModeChange('grid')}
            size="small"
          />
        </Tooltip>
        <Tooltip content="Table view" relationship="label">
          <ToggleButton
            appearance="subtle"
            icon={<TextBulletListLtr24Regular />}
            checked={viewMode === 'table'}
            onClick={() => onViewModeChange('table')}
            size="small"
          />
        </Tooltip>
      </div>
    </div>
  );
}
