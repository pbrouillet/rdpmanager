import {
  makeStyles,
  tokens,
  Button,
  Text,
  Menu,
  MenuTrigger,
  MenuPopover,
  MenuList,
  MenuItem,
} from '@fluentui/react-components';
import { Database24Regular, Dismiss12Regular } from '@fluentui/react-icons';

const useStyles = makeStyles({
  root: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalXS,
    overflowX: 'auto',
    padding: `0 ${tokens.spacingHorizontalM}`,
    backgroundColor: tokens.colorNeutralBackground2,
    borderBottom: `1px solid ${tokens.colorNeutralStroke2}`,
  },
  tab: {
    display: 'inline-flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalXS,
    padding: `${tokens.spacingVerticalXXS} ${tokens.spacingHorizontalXXS}`,
    borderBottom: '2px solid transparent',
    minWidth: 0,
  },
  tabActive: {
    borderBottom: `2px solid ${tokens.colorBrandStroke1}`,
  },
  tabLabel: {
    maxWidth: '220px',
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    whiteSpace: 'nowrap',
  },
  closeBtn: {
    minWidth: '24px',
    width: '24px',
    height: '24px',
  },
  empty: {
    color: tokens.colorNeutralForeground4,
    padding: `${tokens.spacingVerticalS} ${tokens.spacingHorizontalS}`,
  },
});

function basename(path: string): string {
  const normalized = path.replace(/\\/g, '/');
  const parts = normalized.split('/').filter(Boolean);
  return parts.length ? parts[parts.length - 1] : path;
}

interface DatabaseTabsProps {
  databases: string[];
  activeDatabase: string;
  onSelect: (path: string) => void;
  onClose: (path: string) => void;
  onClone: (path: string) => void;
  onCopyPath: (path: string) => void;
}

export function DatabaseTabs({
  databases,
  activeDatabase,
  onSelect,
  onClose,
  onClone,
  onCopyPath,
}: DatabaseTabsProps) {
  const styles = useStyles();

  if (databases.length === 0) {
    return (
      <div className={styles.root}>
        <Text className={styles.empty}>No database opened</Text>
      </div>
    );
  }

  return (
    <div className={styles.root}>
      {databases.map((path) => {
        const isActive = path === activeDatabase;
        return (
          <Menu key={path} openOnContext>
            <MenuTrigger disableButtonEnhancement>
              <div className={`${styles.tab} ${isActive ? styles.tabActive : ''}`}>
                <Button
                  appearance="subtle"
                  icon={<Database24Regular />}
                  onClick={() => onSelect(path)}
                  title={path}
                >
                  <span className={styles.tabLabel}>{basename(path)}</span>
                </Button>
                <Button
                  className={styles.closeBtn}
                  appearance="subtle"
                  icon={<Dismiss12Regular />}
                  onClick={() => onClose(path)}
                  title={`Close ${path}`}
                  aria-label={`Close ${path}`}
                />
              </div>
            </MenuTrigger>
            <MenuPopover>
              <MenuList>
                <MenuItem onClick={() => onClone(path)}>Clone</MenuItem>
                <MenuItem onClick={() => onCopyPath(path)}>Copy Path</MenuItem>
              </MenuList>
            </MenuPopover>
          </Menu>
        );
      })}
    </div>
  );
}
