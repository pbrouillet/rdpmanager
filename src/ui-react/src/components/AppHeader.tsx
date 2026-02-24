import {
  makeStyles,
  tokens,
  Button,
  Text,
} from '@fluentui/react-components';
import {
  DesktopPulse24Regular,
  WeatherMoon24Regular,
  WeatherSunny24Regular,
} from '@fluentui/react-icons';
import type { ThemeMode } from '../theme';

const useStyles = makeStyles({
  header: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalM,
    padding: `${tokens.spacingVerticalS} ${tokens.spacingHorizontalM}`,
    backgroundColor: tokens.colorNeutralBackground2,
    borderBottom: `1px solid ${tokens.colorNeutralStroke2}`,
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
    fontSize: tokens.fontSizeBase500,
    fontWeight: tokens.fontWeightBold,
    color: tokens.colorNeutralForeground1,
    letterSpacing: '0.5px',
  },
  subtitle: {
    color: tokens.colorNeutralForeground3,
    fontSize: tokens.fontSizeBase300,
    marginLeft: tokens.spacingHorizontalS,
  },
  themeButton: {
    marginLeft: 'auto',
    minWidth: '40px',
  },
});

interface AppHeaderProps {
  themeMode: ThemeMode;
  onThemeModeChange: (mode: ThemeMode) => void;
}

export function AppHeader({
  themeMode,
  onThemeModeChange,
}: AppHeaderProps) {
  const styles = useStyles();

  return (
    <header className={styles.header}>
      <div className={styles.logo}>
        <DesktopPulse24Regular className={styles.logoIcon} />
        <Text className={styles.logoText}>RDP Manager</Text>
      </div>
      <Text className={styles.subtitle}>Remote Desktop</Text>
      <Button
        className={styles.themeButton}
        appearance="subtle"
        onClick={() => onThemeModeChange(themeMode === 'dark' ? 'light' : 'dark')}
        title={themeMode === 'dark' ? 'Switch to light mode' : 'Switch to dark mode'}
        aria-label={themeMode === 'dark' ? 'Switch to light mode' : 'Switch to dark mode'}
        icon={themeMode === 'dark' ? <WeatherMoon24Regular /> : <WeatherSunny24Regular />}
      >
        {themeMode === 'dark' ? 'Dark' : 'Light'}
      </Button>
    </header>
  );
}
