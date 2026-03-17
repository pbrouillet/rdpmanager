import { useCallback, useState, useRef, useMemo, type DragEvent } from 'react';
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
  DataGrid,
  DataGridHeader,
  DataGridHeaderCell,
  DataGridBody,
  DataGridRow,
  DataGridCell,
  createTableColumn,
  Popover,
  PopoverTrigger,
  PopoverSurface,
  Button,
  Input,
  Checkbox,
  type TableColumnDefinition,
  type TableColumnSizingOptions,
  type TableColumnId,
} from '@fluentui/react-components';
import {
  PlugConnected24Regular,
  Edit24Regular,
  Delete24Regular,
  EmojiSad24Regular,
  Filter16Regular,
  Filter16Filled,
} from '@fluentui/react-icons';
import type { ConnectionProfile } from '../types';
import type { ConnectionGridProps } from './ConnectionGrid';

// ── localStorage keys ──
const LS_COL_ORDER = 'rdpmanager-column-order';
const LS_COL_WIDTHS = 'rdpmanager-column-widths';

// ── Helpers for persisting / loading ──
type ColumnId = 'name' | 'hostname' | 'port' | 'username' | 'domain' | 'folder';

const DEFAULT_COLUMN_ORDER: ColumnId[] = ['name', 'hostname', 'port', 'username', 'domain', 'folder'];

function loadColumnOrder(): ColumnId[] {
  try {
    const raw = window.localStorage.getItem(LS_COL_ORDER);
    if (raw) {
      const parsed = JSON.parse(raw) as string[];
      // Validate: must contain exactly the known column IDs
      const valid = parsed.filter((id): id is ColumnId =>
        DEFAULT_COLUMN_ORDER.includes(id as ColumnId),
      );
      if (valid.length === DEFAULT_COLUMN_ORDER.length) return valid;
    }
  } catch { /* ignore */ }
  return [...DEFAULT_COLUMN_ORDER];
}

function saveColumnOrder(order: ColumnId[]) {
  window.localStorage.setItem(LS_COL_ORDER, JSON.stringify(order));
}

function loadColumnWidths(): Record<string, number> {
  try {
    const raw = window.localStorage.getItem(LS_COL_WIDTHS);
    if (raw) return JSON.parse(raw);
  } catch { /* ignore */ }
  return {};
}

function saveColumnWidths(widths: Record<string, number>) {
  window.localStorage.setItem(LS_COL_WIDTHS, JSON.stringify(widths));
}

// ── Column metadata ──
const COLUMN_META: Record<ColumnId, { label: string; minWidth: number; idealWidth: number }> = {
  name:     { label: 'Name',     minWidth: 100, idealWidth: 200 },
  hostname: { label: 'Hostname', minWidth: 100, idealWidth: 200 },
  port:     { label: 'Port',     minWidth: 60,  idealWidth: 80  },
  username: { label: 'Username', minWidth: 80,  idealWidth: 150 },
  domain:   { label: 'Domain',   minWidth: 80,  idealWidth: 140 },
  folder:   { label: 'Folder',   minWidth: 80,  idealWidth: 150 },
};

// ── Indexed item wrapper ──
// DataGrid manages its own items array; we need to map back to the original
// index in `connections` for the callback props (onSelect, onEdit, etc.).
interface IndexedConnection {
  originalIndex: number;
  conn: ConnectionProfile;
}

function buildColumnDefs(order: ColumnId[]): TableColumnDefinition<IndexedConnection>[] {
  return order.map((id) =>
    createTableColumn<IndexedConnection>({
      columnId: id,
      renderHeaderCell: () => COLUMN_META[id].label,
      renderCell: (item) => {
        const v = item.conn[id];
        return v == null ? '' : String(v);
      },
      compare: (a, b) => {
        if (id === 'port') return a.conn.port - b.conn.port;
        return String(a.conn[id] ?? '').localeCompare(String(b.conn[id] ?? ''), undefined, { sensitivity: 'base' });
      },
    }),
  );
}

function buildSizingOptions(order: ColumnId[], savedWidths: Record<string, number>): TableColumnSizingOptions {
  const opts: TableColumnSizingOptions = {};
  for (const id of order) {
    const meta = COLUMN_META[id];
    opts[id] = {
      minWidth: meta.minWidth,
      idealWidth: savedWidths[id] ?? meta.idealWidth,
    };
  }
  return opts;
}

// ── Styles ──
const useStyles = makeStyles({
  wrapper: {
    backgroundColor: tokens.colorNeutralBackground2,
    borderTop: `1px solid ${tokens.colorNeutralStroke2}`,
    minHeight: '400px',
    overflow: 'auto',
    height: '100%',
  },
  emptyState: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    justifyContent: 'center',
    gap: tokens.spacingVerticalM,
    padding: tokens.spacingVerticalXXL,
    color: tokens.colorNeutralForeground4,
  },
  emptyIcon: {
    fontSize: '48px',
    opacity: 0.5,
  },
  headerCell: {
    cursor: 'grab',
  },
  headerCellDragOver: {
    borderLeft: `2px solid ${tokens.colorBrandStroke1}`,
  },
  headerCellContent: {
    display: 'flex',
    alignItems: 'center',
    gap: tokens.spacingHorizontalXS,
    width: '100%',
  },
  headerLabel: {
    flex: 1,
    overflow: 'hidden',
    textOverflow: 'ellipsis',
  },
  filterBtn: {
    minWidth: 'auto',
    padding: '2px',
    flexShrink: 0,
  },
  filterBtnActive: {
    color: tokens.colorBrandForeground1,
  },
  filterSurface: {
    display: 'flex',
    flexDirection: 'column',
    gap: tokens.spacingVerticalS,
    width: '240px',
    maxHeight: '360px',
  },
  filterActions: {
    display: 'flex',
    gap: tokens.spacingHorizontalS,
    fontSize: tokens.fontSizeBase200,
  },
  filterLink: {
    cursor: 'pointer',
    color: tokens.colorBrandForeground1,
    fontSize: tokens.fontSizeBase200,
    background: 'none',
    border: 'none',
    padding: 0,
    textDecoration: 'underline',
    ':hover': {
      color: tokens.colorBrandForeground2,
    },
  },
  filterList: {
    overflowY: 'auto',
    maxHeight: '220px',
    display: 'flex',
    flexDirection: 'column',
    gap: '2px',
  },
  filterFooter: {
    display: 'flex',
    justifyContent: 'flex-end',
    gap: tokens.spacingHorizontalS,
    borderTop: `1px solid ${tokens.colorNeutralStroke2}`,
    paddingTop: tokens.spacingVerticalS,
  },
  filterStatus: {
    padding: `${tokens.spacingVerticalXS} ${tokens.spacingHorizontalM}`,
    fontSize: tokens.fontSizeBase200,
    color: tokens.colorNeutralForeground4,
    backgroundColor: tokens.colorNeutralBackground3,
    borderBottom: `1px solid ${tokens.colorNeutralStroke2}`,
  },
});

// ── Column filter popover ──
type ColumnFilters = Partial<Record<ColumnId, Set<string>>>;

function getColumnValue(conn: ConnectionProfile, colId: ColumnId): string {
  const v = conn[colId];
  return v == null ? '' : String(v);
}

interface ColumnFilterPopoverProps {
  columnId: ColumnId;
  distinctValues: string[];
  excluded: Set<string>;
  onApply: (columnId: ColumnId, excluded: Set<string>) => void;
}

function ColumnFilterPopover({ columnId, distinctValues, excluded, onApply }: ColumnFilterPopoverProps) {
  const styles = useStyles();
  const [open, setOpen] = useState(false);
  const [search, setSearch] = useState('');
  const [localExcluded, setLocalExcluded] = useState<Set<string>>(new Set(excluded));

  const isActive = excluded.size > 0;

  // Reset local state when popover opens
  const handleOpenChange = useCallback((_e: unknown, data: { open: boolean }) => {
    if (data.open) {
      setLocalExcluded(new Set(excluded));
      setSearch('');
    }
    setOpen(data.open);
  }, [excluded]);

  const filteredValues = useMemo(() => {
    if (!search) return distinctValues;
    const lower = search.toLowerCase();
    return distinctValues.filter((v) => v.toLowerCase().includes(lower));
  }, [distinctValues, search]);

  const handleToggle = useCallback((value: string, checked: boolean) => {
    setLocalExcluded((prev) => {
      const next = new Set(prev);
      if (checked) {
        next.delete(value);
      } else {
        next.add(value);
      }
      return next;
    });
  }, []);

  const handleSelectAll = useCallback(() => {
    setLocalExcluded(new Set());
  }, []);

  const handleClearAll = useCallback(() => {
    setLocalExcluded(new Set(distinctValues));
  }, [distinctValues]);

  const handleApply = useCallback(() => {
    onApply(columnId, localExcluded);
    setOpen(false);
  }, [columnId, localExcluded, onApply]);

  const handleClearFilter = useCallback(() => {
    onApply(columnId, new Set());
    setOpen(false);
  }, [columnId, onApply]);

  return (
    <Popover open={open} onOpenChange={handleOpenChange} trapFocus>
      <PopoverTrigger disableButtonEnhancement>
        <Button
          appearance="transparent"
          size="small"
          className={`${styles.filterBtn} ${isActive ? styles.filterBtnActive : ''}`}
          icon={isActive ? <Filter16Filled /> : <Filter16Regular />}
          onClick={(e: React.MouseEvent) => e.stopPropagation()}
        />
      </PopoverTrigger>
      <PopoverSurface className={styles.filterSurface} onClick={(e: React.MouseEvent) => e.stopPropagation()}>
        <Input
          placeholder="Search values..."
          size="small"
          value={search}
          onChange={(_e, data) => setSearch(data.value)}
        />
        <div className={styles.filterActions}>
          <button type="button" className={styles.filterLink} onClick={handleSelectAll}>Select All</button>
          <button type="button" className={styles.filterLink} onClick={handleClearAll}>Clear All</button>
        </div>
        <div className={styles.filterList}>
          {filteredValues.map((value) => (
            <Checkbox
              key={value}
              label={value || '(empty)'}
              checked={!localExcluded.has(value)}
              onChange={(_e, data) => handleToggle(value, !!data.checked)}
              size="medium"
            />
          ))}
          {filteredValues.length === 0 && (
            <Text size={200} style={{ color: tokens.colorNeutralForeground4, padding: tokens.spacingVerticalS }}>
              No matching values
            </Text>
          )}
        </div>
        <div className={styles.filterFooter}>
          <Button size="small" appearance="secondary" onClick={handleClearFilter} disabled={!isActive}>
            Clear Filter
          </Button>
          <Button size="small" appearance="primary" onClick={handleApply}>
            Apply
          </Button>
        </div>
      </PopoverSurface>
    </Popover>
  );
}

// ── Component ──
export function ConnectionTable({
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

  // Column order state
  const [columnOrder, setColumnOrder] = useState<ColumnId[]>(loadColumnOrder);

  // Column width persistence
  const savedWidthsRef = useRef(loadColumnWidths());
  const [sizingOptions, setSizingOptions] = useState<TableColumnSizingOptions>(
    () => buildSizingOptions(columnOrder, savedWidthsRef.current),
  );

  // Memoize column definitions based on order
  const columns = useMemo(() => buildColumnDefs(columnOrder), [columnOrder]);

  // ── Column filter state ──
  const [columnFilters, setColumnFilters] = useState<ColumnFilters>({});

  // Compute distinct values per column from all connections (pre-filter)
  const distinctValuesMap = useMemo(() => {
    const map: Record<ColumnId, string[]> = {} as Record<ColumnId, string[]>;
    for (const colId of DEFAULT_COLUMN_ORDER) {
      const valSet = new Set<string>();
      for (const conn of connections) {
        valSet.add(getColumnValue(conn, colId));
      }
      map[colId] = [...valSet].sort((a, b) => a.localeCompare(b, undefined, { sensitivity: 'base' }));
    }
    return map;
  }, [connections]);

  const handleFilterApply = useCallback((colId: ColumnId, excluded: Set<string>) => {
    setColumnFilters((prev) => {
      const next = { ...prev };
      if (excluded.size === 0) {
        delete next[colId];
      } else {
        next[colId] = excluded;
      }
      return next;
    });
  }, []);

  const hasActiveFilters = Object.keys(columnFilters).length > 0;

  // Wrap connections with original indices
  const allItems: IndexedConnection[] = useMemo(
    () => connections.map((conn, i) => ({ originalIndex: i, conn })),
    [connections],
  );

  // Apply column filters
  const items: IndexedConnection[] = useMemo(() => {
    if (!hasActiveFilters) return allItems;
    return allItems.filter(({ conn }) => {
      for (const colId of Object.keys(columnFilters) as ColumnId[]) {
        const excluded = columnFilters[colId];
        if (excluded && excluded.has(getColumnValue(conn, colId))) {
          return false;
        }
      }
      return true;
    });
  }, [allItems, columnFilters, hasActiveFilters]);

  // ── Column resize handler ──
  const handleColumnResize = useCallback(
    (_e: KeyboardEvent | TouchEvent | MouseEvent | undefined, data: { columnId: TableColumnId; width: number }) => {
      savedWidthsRef.current[String(data.columnId)] = data.width;
      saveColumnWidths(savedWidthsRef.current);
    },
    [],
  );

  // ── Column drag-to-reorder state ──
  const dragColRef = useRef<ColumnId | null>(null);
  const [dragOverCol, setDragOverCol] = useState<ColumnId | null>(null);

  const handleHeaderDragStart = useCallback((colId: ColumnId, e: React.DragEvent) => {
    dragColRef.current = colId;
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/x-column-id', colId);
  }, []);

  const handleHeaderDragOver = useCallback((colId: ColumnId, e: React.DragEvent) => {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
    setDragOverCol(colId);
  }, []);

  const handleHeaderDrop = useCallback((targetId: ColumnId, e: React.DragEvent) => {
    e.preventDefault();
    const sourceId = dragColRef.current;
    setDragOverCol(null);
    dragColRef.current = null;
    if (!sourceId || sourceId === targetId) return;

    setColumnOrder((prev) => {
      const next = [...prev];
      const srcIdx = next.indexOf(sourceId);
      const tgtIdx = next.indexOf(targetId);
      if (srcIdx === -1 || tgtIdx === -1) return prev;
      next.splice(srcIdx, 1);
      next.splice(tgtIdx, 0, sourceId);
      saveColumnOrder(next);
      // Rebuild sizing options for new order
      setSizingOptions(buildSizingOptions(next, savedWidthsRef.current));
      return next;
    });
  }, []);

  const handleHeaderDragEnd = useCallback(() => {
    setDragOverCol(null);
    dragColRef.current = null;
  }, []);

  // ── Empty state ──
  if (connections.length === 0) {
    return (
      <div className={styles.wrapper}>
        <div className={styles.emptyState}>
          <EmojiSad24Regular className={styles.emptyIcon} />
          <Text>No saved connections yet</Text>
          <Text size={200}>Click "New Connection" to get started</Text>
        </div>
      </div>
    );
  }

  return (
    <div className={styles.wrapper}>
      {hasActiveFilters && (
        <div className={styles.filterStatus}>
          Showing {items.length} of {connections.length} connections (filtered)
        </div>
      )}
      <DataGrid
        items={items}
        columns={columns}
        getRowId={(item) => item.conn.name}
        sortable
        resizableColumns
        columnSizingOptions={sizingOptions}
        onColumnResize={handleColumnResize}
        resizableColumnsOptions={{ autoFitColumns: true }}
        defaultSortState={{ sortColumn: 'name', sortDirection: 'ascending' }}
        style={{ minWidth: '100%' }}
      >
        <DataGridHeader>
          <DataGridRow>
            {({ columnId, renderHeaderCell }) => (
              <DataGridHeaderCell
                key={String(columnId)}
                className={`${styles.headerCell} ${dragOverCol === columnId ? styles.headerCellDragOver : ''}`}
                draggable
                onDragStart={(e: React.DragEvent) => handleHeaderDragStart(columnId as ColumnId, e)}
                onDragOver={(e: React.DragEvent) => handleHeaderDragOver(columnId as ColumnId, e)}
                onDrop={(e: React.DragEvent) => handleHeaderDrop(columnId as ColumnId, e)}
                onDragEnd={handleHeaderDragEnd}
                onDragLeave={() => setDragOverCol(null)}
              >
                <span className={styles.headerCellContent}>
                  <span className={styles.headerLabel}>{renderHeaderCell()}</span>
                  <ColumnFilterPopover
                    columnId={columnId as ColumnId}
                    distinctValues={distinctValuesMap[columnId as ColumnId] ?? []}
                    excluded={columnFilters[columnId as ColumnId] ?? new Set()}
                    onApply={handleFilterApply}
                  />
                </span>
              </DataGridHeaderCell>
            )}
          </DataGridRow>
        </DataGridHeader>
        <DataGridBody<IndexedConnection>>
          {({ item }) => (
            <ConnectionRow
              key={item.conn.name}
              item={item}
              isSelected={selectedIndex === item.originalIndex}
              onSelect={onSelect}
              onDoubleClick={onDoubleClick}
              onConnect={onConnect}
              onEdit={onEdit}
              onDelete={onDelete}
              onDragStartConnection={onDragStartConnection}
            />
          )}
        </DataGridBody>
      </DataGrid>
    </div>
  );
}

// ── Row component ──
interface ConnectionRowProps {
  item: IndexedConnection;
  isSelected: boolean;
  onSelect: (index: number) => void;
  onDoubleClick: (index: number) => void;
  onConnect: (index: number) => void;
  onEdit: (index: number) => void;
  onDelete: (index: number) => void;
  onDragStartConnection: (name: string) => void;
}

function ConnectionRow({
  item,
  isSelected,
  onSelect,
  onDoubleClick,
  onConnect,
  onEdit,
  onDelete,
  onDragStartConnection,
}: ConnectionRowProps) {
  const { originalIndex, conn } = item;

  const handleClick = useCallback(() => onSelect(originalIndex), [onSelect, originalIndex]);
  const handleDblClick = useCallback(() => onDoubleClick(originalIndex), [onDoubleClick, originalIndex]);
  const handleDragStart = useCallback(
    (event: DragEvent<HTMLElement>) => {
      event.dataTransfer.effectAllowed = 'move';
      event.dataTransfer.setData('application/x-rdp-connection', conn.name);
      event.dataTransfer.setData('text/plain', conn.name);
      onDragStartConnection(conn.name);
    },
    [conn.name, onDragStartConnection],
  );

  return (
    <Menu openOnContext>
      <MenuTrigger disableButtonEnhancement>
        <DataGridRow<IndexedConnection>
          appearance={isSelected ? 'brand' : undefined}
          onClick={handleClick}
          onDoubleClick={handleDblClick}
          draggable
          onDragStart={handleDragStart}
          style={{ cursor: 'pointer' }}
        >
          {({ renderCell }) => <DataGridCell>{renderCell(item)}</DataGridCell>}
        </DataGridRow>
      </MenuTrigger>
      <MenuPopover>
        <MenuList>
          <MenuItem icon={<PlugConnected24Regular />} onClick={() => onConnect(originalIndex)}>
            Connect
          </MenuItem>
          <MenuItem icon={<Edit24Regular />} onClick={() => onEdit(originalIndex)}>
            Edit
          </MenuItem>
          <MenuDivider />
          <MenuItem icon={<Delete24Regular />} onClick={() => onDelete(originalIndex)}>
            Delete
          </MenuItem>
        </MenuList>
      </MenuPopover>
    </Menu>
  );
}
