import {
  makeStyles,
  tokens,
  Button,
  Text,
  Badge,
  Tooltip,
} from '@fluentui/react-components';
import {
  DesktopMac24Regular,
  Home24Regular,
  Dismiss12Regular,
} from '@fluentui/react-icons';

const useStyles = makeStyles({
  root: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalXS,
    overflowX: 'auto',
    padding: `0 ${tokens.spacingHorizontalM}`,
    backgroundColor: tokens.colorNeutralBackground3,
    borderBottom: `1px solid ${tokens.colorNeutralStroke2}`,
    minHeight: '36px',
  },
  tab: {
    display: 'inline-flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalXS,
    padding: `${tokens.spacingVerticalXXS} ${tokens.spacingHorizontalXXS}`,
    borderBottom: '2px solid transparent',
    minWidth: 0,
    cursor: 'pointer',
  },
  tabActive: {
    borderBottom: `2px solid ${tokens.colorBrandStroke1}`,
  },
  tabLabel: {
    maxWidth: '200px',
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    whiteSpace: 'nowrap',
  },
  closeBtn: {
    minWidth: '20px',
    width: '20px',
    height: '20px',
  },
  stateBadge: {
    marginLeft: tokens.spacingHorizontalXXS,
  },
});

export interface RdpSession {
  id: string;
  hostname: string;
  state: 'connecting' | 'connected' | 'disconnecting' | 'disconnected' | 'error';
}

interface SessionTabsProps {
  sessions: RdpSession[];
  activeTab: string; // 'home' or session id
  onSelectHome: () => void;
  onSelectSession: (id: string) => void;
  onCloseSession: (id: string) => void;
}

function stateColor(state: RdpSession['state']): 'success' | 'warning' | 'danger' | 'informative' {
  switch (state) {
    case 'connected':
      return 'success';
    case 'connecting':
      return 'informative';
    case 'disconnecting':
      return 'warning';
    case 'error':
      return 'danger';
    default:
      return 'informative';
  }
}

export function SessionTabs({
  sessions,
  activeTab,
  onSelectHome,
  onSelectSession,
  onCloseSession,
}: SessionTabsProps) {
  const styles = useStyles();

  if (sessions.length === 0) return null;

  return (
    <div className={styles.root}>
      {/* Home tab */}
      <div className={`${styles.tab} ${activeTab === 'home' ? styles.tabActive : ''}`}>
        <Button
          appearance="subtle"
          icon={<Home24Regular />}
          onClick={onSelectHome}
          size="small"
        >
          Home
        </Button>
      </div>

      {/* RDP session tabs */}
      {sessions.map((session) => {
        const isActive = activeTab === session.id;
        return (
          <div
            key={session.id}
            className={`${styles.tab} ${isActive ? styles.tabActive : ''}`}
          >
            <Tooltip content={`${session.hostname} (${session.state})`} relationship="label">
              <Button
                appearance="subtle"
                icon={<DesktopMac24Regular />}
                onClick={() => onSelectSession(session.id)}
                size="small"
              >
                <span className={styles.tabLabel}>{session.hostname}</span>
              </Button>
            </Tooltip>
            <Badge
              className={styles.stateBadge}
              color={stateColor(session.state)}
              size="tiny"
              shape="circular"
            />
            <Button
              className={styles.closeBtn}
              appearance="subtle"
              icon={<Dismiss12Regular />}
              onClick={(ev) => {
                ev.stopPropagation();
                onCloseSession(session.id);
              }}
              size="small"
              title="Disconnect"
              aria-label={`Disconnect ${session.hostname}`}
            />
          </div>
        );
      })}
    </div>
  );
}
