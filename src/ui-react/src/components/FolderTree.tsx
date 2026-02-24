import { useMemo, useState, type DragEvent } from 'react';
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
import {
  Folder24Regular,
  FolderOpen24Regular,
  AppsList24Regular,
  Add24Regular,
  ArrowExportRtl24Regular,
  Edit24Regular,
  Delete24Regular,
} from '@fluentui/react-icons';
import type { ConnectionProfile } from '../types';

const useStyles = makeStyles({
  root: {
    height: '100%',
    display: 'flex',
    flexDirection: 'column',
    backgroundColor: tokens.colorNeutralBackground1,
    overflow: 'hidden',
  },
  header: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: `${tokens.spacingVerticalXS} ${tokens.spacingHorizontalS}`,
    borderBottom: `1px solid ${tokens.colorNeutralStroke2}`,
  },
  headerTitle: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalXS,
    fontWeight: tokens.fontWeightSemibold,
  },
  list: {
    padding: tokens.spacingVerticalXXS,
    overflowY: 'auto',
    flex: 1,
  },
  item: {
    width: '100%',
    justifyContent: 'flex-start',
    marginBottom: 0,
  },
  itemDropZone: {
    borderBottom: `1px solid ${tokens.colorNeutralStroke2}`,
  },
  itemDropZoneActive: {
    outline: `2px solid ${tokens.colorBrandStroke1}`,
    outlineOffset: '-2px',
    backgroundColor: tokens.colorBrandBackground2,
  },
  itemLabel: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalXS,
    minWidth: 0,
  },
  itemName: {
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    whiteSpace: 'nowrap',
  },
  itemCount: {
    marginLeft: 'auto',
    color: tokens.colorNeutralForeground4,
  },
});

interface FolderNode {
  path: string;
  name: string;
  depth: number;
  count: number;
}

function normalizeFolderPath(folder: string): string {
  return folder
    .split('/')
    .map((part) => part.trim())
    .filter(Boolean)
    .join('/');
}

function buildFolderNodes(connections: ConnectionProfile[], folders: string[]): FolderNode[] {
  const pathCount = new Map<string, number>();

  for (const conn of connections) {
    const normalized = normalizeFolderPath(conn.folder || '');
    if (!normalized) {
      continue;
    }

    const parts = normalized.split('/');
    for (let i = 0; i < parts.length; i += 1) {
      const path = parts.slice(0, i + 1).join('/');
      pathCount.set(path, (pathCount.get(path) || 0) + 1);
    }
  }

  for (const folder of folders) {
    const normalized = normalizeFolderPath(folder || '');
    if (!normalized) {
      continue;
    }

    const parts = normalized.split('/');
    for (let i = 0; i < parts.length; i += 1) {
      const path = parts.slice(0, i + 1).join('/');
      if (!pathCount.has(path)) {
        pathCount.set(path, 0);
      }
    }
  }

  return Array.from(pathCount.entries())
    .map(([path, count]) => {
      const parts = path.split('/');
      return {
        path,
        name: parts[parts.length - 1],
        depth: parts.length,
        count,
      } satisfies FolderNode;
    })
    .sort((a, b) => a.path.localeCompare(b.path, undefined, { sensitivity: 'base' }));
}

interface FolderTreeProps {
  connections: ConnectionProfile[];
  folders: string[];
  selectedFolder: string;
  onSelectFolder: (folder: string) => void;
  onCreateFolder: () => void;
  onDropConnectionToFolder: (folder: string, connectionName: string) => void;
  onDropFolderToFolder: (sourceFolder: string, targetParentFolder: string) => void;
  onRenameFolder: (folderPath: string) => void;
  onDeleteFolder: (folderPath: string) => void;
}

export function FolderTree({
  connections,
  folders,
  selectedFolder,
  onSelectFolder,
  onCreateFolder,
  onDropConnectionToFolder,
  onDropFolderToFolder,
  onRenameFolder,
  onDeleteFolder,
}: FolderTreeProps) {
  const styles = useStyles();
  const [dropTargetFolder, setDropTargetFolder] = useState<string | null>(null);

  const nodes = useMemo(() => buildFolderNodes(connections, folders), [connections, folders]);
  const rootCount = connections.length;

  const handleDragOverFolder = (event: DragEvent<HTMLElement>, folder: string) => {
    event.preventDefault();
    event.dataTransfer.dropEffect = 'move';
    setDropTargetFolder(folder);
  };

  const handleDropFolder = (event: DragEvent<HTMLElement>, folder: string) => {
    event.preventDefault();
    setDropTargetFolder(null);
    const draggedFolder = event.dataTransfer.getData('application/x-rdp-folder');
    if (draggedFolder) {
      onDropFolderToFolder(draggedFolder, folder);
      return;
    }

    const connectionName = event.dataTransfer.getData('text/plain');
    if (!connectionName) {
      return;
    }
    onDropConnectionToFolder(folder, connectionName);
  };

  return (
    <div className={styles.root}>
      <div className={styles.header}>
        <div className={styles.headerTitle}>
          <Folder24Regular />
          <Text>Folders</Text>
        </div>
        <Button
          appearance="subtle"
          size="small"
          icon={<Add24Regular />}
          onClick={onCreateFolder}
          title="Create folder"
          aria-label="Create folder"
        />
      </div>

      <div className={styles.list}>
        <div
          className={`${styles.itemDropZone} ${dropTargetFolder === '' ? styles.itemDropZoneActive : ''}`}
          onDragOver={(event) => handleDragOverFolder(event, '')}
          onDragLeave={() => setDropTargetFolder((current) => (current === '' ? null : current))}
          onDrop={(event) => handleDropFolder(event, '')}
        >
          <Button
            className={styles.item}
            appearance={selectedFolder === '' ? 'primary' : 'subtle'}
            icon={<AppsList24Regular />}
            onClick={() => onSelectFolder('')}
          >
            <span className={styles.itemLabel}>
              <span className={styles.itemName}>All Connections</span>
              <span className={styles.itemCount}>{rootCount}</span>
            </span>
          </Button>
        </div>

        {nodes.map((node) => {
          const isSelected = selectedFolder === node.path;
          return (
            <div
              key={node.path}
              className={`${styles.itemDropZone} ${dropTargetFolder === node.path ? styles.itemDropZoneActive : ''}`}
              onDragOver={(event) => handleDragOverFolder(event, node.path)}
              onDragLeave={() =>
                setDropTargetFolder((current) => (current === node.path ? null : current))
              }
              onDrop={(event) => handleDropFolder(event, node.path)}
            >
              <Menu openOnContext>
                <MenuTrigger disableButtonEnhancement>
                  <Button
                    className={styles.item}
                    appearance={isSelected ? 'primary' : 'subtle'}
                    icon={isSelected ? <FolderOpen24Regular /> : <Folder24Regular />}
                    onClick={() => onSelectFolder(node.path)}
                    style={{ paddingLeft: `${node.depth * 14}px` }}
                    draggable
                    onDragStart={(event) => {
                      event.dataTransfer.effectAllowed = 'move';
                      event.dataTransfer.setData('application/x-rdp-folder', node.path);
                      event.dataTransfer.setData('text/plain', node.path);
                    }}
                  >
                    <span className={styles.itemLabel}>
                      <span className={styles.itemName}>{node.name}</span>
                      <span className={styles.itemCount}>{node.count}</span>
                    </span>
                    <ArrowExportRtl24Regular style={{ marginLeft: 'auto', opacity: 0.35 }} />
                  </Button>
                </MenuTrigger>
                <MenuPopover>
                  <MenuList>
                    <MenuItem icon={<Edit24Regular />} onClick={() => onRenameFolder(node.path)}>
                      Rename Folder
                    </MenuItem>
                    <MenuItem icon={<Delete24Regular />} onClick={() => onDeleteFolder(node.path)}>
                      Delete Folder
                    </MenuItem>
                  </MenuList>
                </MenuPopover>
              </Menu>
            </div>
          );
        })}
      </div>
    </div>
  );
}
