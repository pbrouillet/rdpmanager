import {
  makeStyles,
  tokens,
  Badge,
  Text,
  Switch,
} from '@fluentui/react-components';
import { DesktopPulse24Regular } from '@fluentui/react-icons';
import type { ThemeMode } from '../theme';

const useStyles = makeStyles({
  header: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalL,
    padding: `${tokens.spacingVerticalM} ${tokens.spacingHorizontalL}`,
    backgroundColor: tokens.colorNeutralBackground2,
    borderRadius: tokens.borderRadiusLarge,
    border: `1px solid ${tokens.colorNeutralStroke1}`,
  },
  logo: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalS,
  },
  logoIcon: {
    color: tokens.colorBrandForeground1,
    fontSize: '28px',
  },
  logoText: {
    fontSize: tokens.fontSizeBase600,
    fontWeight: tokens.fontWeightBold,
    color: tokens.colorBrandForeground1,
    letterSpacing: '2px',
  },
  subtitle: {
    color: tokens.colorNeutralForeground3,
    fontSize: tokens.fontSizeBase300,
    paddingLeft: tokens.spacingHorizontalM,
    borderLeft: `1px solid ${tokens.colorNeutralStroke1}`,
  },
  status: {
    marginLeft: 'auto',
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalS,
    padding: `${tokens.spacingVerticalS} ${tokens.spacingHorizontalM}`,
    backgroundColor: tokens.colorNeutralBackground3,
    borderRadius: tokens.borderRadiusMedium,
  },
  themeSwitch: {
    marginLeft: tokens.spacingHorizontalM,
  },
});

interface AppHeaderProps {
  statusText: string;
  statusOnline: boolean;
  themeMode: ThemeMode;
  onThemeModeChange: (mode: ThemeMode) => void;
}

export function AppHeader({
  statusText,
  statusOnline,
  themeMode,
  onThemeModeChange,
}: AppHeaderProps) {
  const styles = useStyles();

  return (
    <header className={styles.header}>
      <div className={styles.logo}>
        <DesktopPulse24Regular className={styles.logoIcon} />
        <span className={styles.logoText}>RDP</span>
      </div>
      <span className={styles.subtitle}>Remote Desktop Interface</span>
      <Switch
        className={styles.themeSwitch}
        label="Dark mode"
        checked={themeMode === 'dark'}
        onChange={(_, data) => onThemeModeChange(data.checked ? 'dark' : 'light')}
      />
      <div className={styles.status}>
        <Badge
          size="tiny"
          color={statusOnline ? 'success' : 'warning'}
          appearance="filled"
        />
        <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>
          {statusText}
        </Text>
      </div>
    </header>
  );
}
