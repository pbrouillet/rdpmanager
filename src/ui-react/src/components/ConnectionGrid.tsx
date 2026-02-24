import { useCallback, type DragEvent } from 'react';
import {
  makeStyles,
  tokens,
  Text,
  Menu,
  MenuTrigger,
  MenuPopover,
  MenuList,
  MenuItem,
  MenuDivider,
} from '@fluentui/react-components';
import {
  DesktopMac24Regular,
  PlugConnected24Regular,
  Edit24Regular,
  Delete24Regular,
  EmojiSad24Regular,
} from '@fluentui/react-icons';
import type { ConnectionProfile } from '../types';

const useStyles = makeStyles({
  grid: {
    display: 'grid',
    gridTemplateColumns: 'repeat(auto-fill, minmax(180px, 1fr))',
    gap: tokens.spacingHorizontalM,
    padding: tokens.spacingHorizontalM,
    backgroundColor: tokens.colorNeutralBackground2,
    borderTop: `1px solid ${tokens.colorNeutralStroke2}`,
    minHeight: '400px',
  },
  card: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    gap: tokens.spacingVerticalS,
    padding: tokens.spacingVerticalM,
    backgroundColor: tokens.colorNeutralBackground1,
    border: `1px solid ${tokens.colorNeutralStroke2}`,
    cursor: 'pointer',
    transitionProperty: 'background-color, border-color',
    transitionDuration: tokens.durationFaster,
    ':hover': {
      backgroundColor: tokens.colorNeutralBackground1Hover,
      border: `1px solid ${tokens.colorBrandStroke1}`,
    },
  },
  cardSelected: {
    border: `1px solid ${tokens.colorBrandStroke1}`,
    backgroundColor: tokens.colorBrandBackground2,
  },
  iconWrapper: {
    width: '44px',
    height: '44px',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: tokens.colorNeutralBackground1,
    border: `1px solid ${tokens.colorNeutralStroke2}`,
    color: tokens.colorBrandForeground1,
    fontSize: '20px',
  },
  name: {
    fontWeight: tokens.fontWeightSemibold,
    textAlign: 'center' as const,
    wordBreak: 'break-word' as const,
  },
  host: {
    textAlign: 'center' as const,
    wordBreak: 'break-word' as const,
  },
  emptyState: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    justifyContent: 'center',
    gap: tokens.spacingVerticalM,
    padding: tokens.spacingVerticalXXL,
    gridColumn: '1 / -1',
    color: tokens.colorNeutralForeground4,
    border: `1px solid ${tokens.colorNeutralStroke2}`,
    backgroundColor: tokens.colorNeutralBackground1,
  },
  emptyIcon: {
    fontSize: '48px',
    opacity: 0.5,
  },
});

interface ConnectionGridProps {
  connections: ConnectionProfile[];
  selectedIndex: number | null;
  onSelect: (index: number) => void;
  onDoubleClick: (index: number) => void;
  onConnect: (index: number) => void;
  onEdit: (index: number) => void;
  onDelete: (index: number) => void;
  onDragStartConnection: (name: string) => void;
}

export function ConnectionGrid({
  connections,
  selectedIndex,
  onSelect,
  onDoubleClick,
  onConnect,
  onEdit,
  onDelete,
  onDragStartConnection,
}: ConnectionGridProps) {
  const styles = useStyles();

  if (connections.length === 0) {
    return (
      <div className={styles.grid}>
        <div className={styles.emptyState}>
          <EmojiSad24Regular className={styles.emptyIcon} />
          <Text>No saved connections yet</Text>
          <Text size={200}>Click "New Connection" to get started</Text>
        </div>
      </div>
    );
  }

  return (
    <div className={styles.grid}>
      {connections.map((conn, index) => (
        <ConnectionCard
          key={conn.name}
          conn={conn}
          index={index}
          isSelected={selectedIndex === index}
          onSelect={onSelect}
          onDoubleClick={onDoubleClick}
          onConnect={onConnect}
          onEdit={onEdit}
          onDelete={onDelete}
          onDragStartConnection={onDragStartConnection}
        />
      ))}
    </div>
  );
}

interface ConnectionCardProps {
  conn: ConnectionProfile;
  index: number;
  isSelected: boolean;
  onSelect: (index: number) => void;
  onDoubleClick: (index: number) => void;
  onConnect: (index: number) => void;
  onEdit: (index: number) => void;
  onDelete: (index: number) => void;
  onDragStartConnection: (name: string) => void;
}

function ConnectionCard({
  conn,
  index,
  isSelected,
  onSelect,
  onDoubleClick,
  onConnect,
  onEdit,
  onDelete,
  onDragStartConnection,
}: ConnectionCardProps) {
  const styles = useStyles();

  const handleClick = useCallback(() => onSelect(index), [onSelect, index]);
  const handleDblClick = useCallback(() => onDoubleClick(index), [onDoubleClick, index]);
  const handleDragStart = useCallback(
    (event: DragEvent<HTMLDivElement>) => {
      event.dataTransfer.effectAllowed = 'move';
      event.dataTransfer.setData('application/x-rdp-connection', conn.name);
      event.dataTransfer.setData('text/plain', conn.name);
      onDragStartConnection(conn.name);
    },
    [conn.name, onDragStartConnection]
  );

  return (
    <Menu openOnContext>
      <MenuTrigger disableButtonEnhancement>
        <div
          className={`${styles.card} ${isSelected ? styles.cardSelected : ''}`}
          onClick={handleClick}
          onDoubleClick={handleDblClick}
          draggable
          onDragStart={handleDragStart}
        >
          <div className={styles.iconWrapper}>
            <DesktopMac24Regular />
          </div>
          <Text className={styles.name}>{conn.name}</Text>
          <Text size={200} className={styles.host}>
            {conn.hostname}:{conn.port}
          </Text>
        </div>
      </MenuTrigger>
      <MenuPopover>
        <MenuList>
          <MenuItem icon={<PlugConnected24Regular />} onClick={() => onConnect(index)}>
            Connect
          </MenuItem>
          <MenuItem icon={<Edit24Regular />} onClick={() => onEdit(index)}>
            Edit
          </MenuItem>
          <MenuDivider />
          <MenuItem icon={<Delete24Regular />} onClick={() => onDelete(index)}>
            Delete
          </MenuItem>
        </MenuList>
      </MenuPopover>
    </Menu>
  );
}
