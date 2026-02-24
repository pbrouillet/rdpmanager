import {
  makeStyles,
  tokens,
  Button,
  Text,
} from '@fluentui/react-components';
import { Database24Regular, Dismiss12Regular } from '@fluentui/react-icons';

const useStyles = makeStyles({
  root: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalXS,
    overflowX: 'auto',
    padding: tokens.spacingVerticalXS,
    backgroundColor: tokens.colorNeutralBackground2,
    border: `1px solid ${tokens.colorNeutralStroke1}`,
    borderRadius: tokens.borderRadiusLarge,
  },
  tab: {
    display: 'inline-flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalXS,
    padding: `${tokens.spacingVerticalXS} ${tokens.spacingHorizontalS}`,
    borderRadius: tokens.borderRadiusMedium,
    border: `1px solid ${tokens.colorNeutralStroke1}`,
    backgroundColor: tokens.colorNeutralBackground3,
    minWidth: 0,
  },
  tabActive: {
    backgroundColor: tokens.colorBrandBackground2,
    border: `1px solid ${tokens.colorBrandStroke1}`,
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
    padding: `${tokens.spacingVerticalXS} ${tokens.spacingHorizontalS}`,
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
}

export function DatabaseTabs({ databases, activeDatabase, onSelect, onClose }: DatabaseTabsProps) {
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
          <div key={path} className={`${styles.tab} ${isActive ? styles.tabActive : ''}`}>
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
        );
      })}
    </div>
  );
}
