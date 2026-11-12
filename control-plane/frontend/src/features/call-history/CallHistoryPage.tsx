import { AlertTriangle, CheckCheck, History } from "lucide-react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { toast } from "sonner";

import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import {
  CALL_HISTORY_POLL_INTERVAL_MS,
  useGetAllCallsQuery,
  useGetCallHistoryQuery,
} from "@/features/call-history/api";
import { CallHistoryDetailDialog } from "@/features/call-history/components/CallHistoryDetailDialog";
import { CallHistoryPagination } from "@/features/call-history/components/CallHistoryPagination";
import { CallHistoryStatusFilter } from "@/features/call-history/components/CallHistoryStatusFilter";
import { CallHistoryTable } from "@/features/call-history/components/CallHistoryTable";
import type {
  CallHistorySortField,
  CallRecord,
  CallStatus,
  SortDirection,
} from "@/features/call-history/types";
import {
  isCallNew,
  useLastSeenCheckpoint,
} from "@/features/call-history/unseen";
import { getApiErrorMessage } from "@/lib/api";

const PAGE_SIZE = 10;

export function CallHistoryPage() {
  const [statuses, setStatuses] = useState<CallStatus[]>([]);
  const [sortField, setSortField] = useState<CallHistorySortField>("timestamp");
  const [sortDir, setSortDir] = useState<SortDirection>("desc");
  const [page, setPage] = useState(1);
  const [selectedCall, setSelectedCall] = useState<CallRecord | undefined>(
    undefined,
  );
  const callRowRefs = useRef(new Map<string, HTMLTableRowElement>());
  const jumpTimer = useRef<ReturnType<typeof setTimeout> | undefined>();
  const [scrollTargetCallId, setScrollTargetCallId] = useState<string>();
  const [highlightedCallId, setHighlightedCallId] = useState<string>();
  const [lastSeenAt, markAllAsRead] = useLastSeenCheckpoint();
  const { data: allCalls } = useGetAllCallsQuery(undefined, {
    pollingInterval: CALL_HISTORY_POLL_INTERVAL_MS,
  });
  const hasUnread = useMemo(
    () => (allCalls ?? []).some((call) => isCallNew(call, lastSeenAt)),
    [allCalls, lastSeenAt],
  );

  const { data, isLoading, isError, error } = useGetCallHistoryQuery(
    {
      statuses,
      sortField,
      sortDir,
      page,
      pageSize: PAGE_SIZE,
    },
    { pollingInterval: CALL_HISTORY_POLL_INTERVAL_MS },
  );
  const calls = useMemo(() => data?.rows ?? [], [data?.rows]);
  const totalCount = data?.totalCount ?? 0;

  const handleStatusChange = useCallback((next: CallStatus[]) => {
    setStatuses(next);
    setPage(1);
  }, []);

  const handleSortChange = useCallback(
    (field: CallHistorySortField) => {
      setSortField(field);
      setSortDir((prevDir) =>
        field === sortField ? (prevDir === "asc" ? "desc" : "asc") : "desc",
      );
      setPage(1);
    },
    [sortField],
  );

  const closeDetailDialog = useCallback((open: boolean) => {
    if (!open) setSelectedCall(undefined);
  }, []);

  const registerCallRow = useCallback(
    (callId: string, row: HTMLTableRowElement | null) => {
      if (row) {
        callRowRefs.current.set(callId, row);
      } else {
        callRowRefs.current.delete(callId);
      }
    },
    [],
  );

  const jumpToCall = useCallback(
    (call: CallRecord) => {
      if (!calls.some((candidate) => candidate.id === call.id)) {
        toast.error(
          "Bridged session is not on this page. Select its page to inspect it.",
        );
        return;
      }

      setSelectedCall(undefined);
      setScrollTargetCallId(call.id);
    },
    [calls],
  );

  useEffect(() => {
    if (!scrollTargetCallId) return;

    const row = callRowRefs.current.get(scrollTargetCallId);
    if (!row) return;

    row.scrollIntoView({ behavior: "smooth", block: "center" });
    setHighlightedCallId(scrollTargetCallId);
    setScrollTargetCallId(undefined);

    if (jumpTimer.current) clearTimeout(jumpTimer.current);
    jumpTimer.current = setTimeout(() => setHighlightedCallId(undefined), 2000);
  }, [scrollTargetCallId]);

  useEffect(() => {
    return () => {
      if (jumpTimer.current) clearTimeout(jumpTimer.current);
    };
  }, []);

  return (
    <div className="space-y-6">
      <div className="flex items-start justify-between gap-4">
        <div className="flex items-center gap-3">
          <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-lg bg-primary/10 text-primary">
            <History className="h-5 w-5" />
          </div>
          <div>
            <h1 className="text-2xl font-semibold tracking-tight">
              Call History
            </h1>
            <p className="text-sm text-muted-foreground">
              Past call attempts handled by the B2BUA.
            </p>
          </div>
        </div>
        <CallHistoryStatusFilter
          selected={statuses}
          onChange={handleStatusChange}
        />
      </div>

      {!isLoading && !isError && (
        <div className="flex items-center justify-between gap-2">
          <div className="flex items-center gap-2 text-sm text-muted-foreground">
            <span className="font-mono tabular-nums text-foreground">
              {totalCount}
            </span>
            {totalCount === 1 ? "call" : "calls"}
            {statuses.length > 0 ? " matching filters" : " total"}
          </div>
          {hasUnread && (
            <Button variant="outline" size="sm" onClick={markAllAsRead}>
              <CheckCheck className="mr-2 h-4 w-4" />
              Mark all as read
            </Button>
          )}
        </div>
      )}

      {isLoading ? (
        <div className="space-y-2 rounded-md border p-2">
          <Skeleton className="h-8 w-full" />
          <Skeleton className="h-10 w-full" />
          <Skeleton className="h-10 w-full" />
          <Skeleton className="h-10 w-full" />
        </div>
      ) : isError ? (
        <div className="flex h-32 flex-col items-center justify-center gap-2 rounded-md border border-dashed text-sm text-destructive">
          <AlertTriangle className="h-5 w-5" />
          {getApiErrorMessage(error)}
        </div>
      ) : (
        <CallHistoryTable
          calls={calls}
          sortField={sortField}
          sortDir={sortDir}
          onSortChange={handleSortChange}
          onSelectCall={setSelectedCall}
          onCallRowChange={registerCallRow}
          highlightedCallId={highlightedCallId}
          lastSeenAt={lastSeenAt}
        />
      )}

      {!isLoading && totalCount > 0 && (
        <CallHistoryPagination
          page={page}
          pageSize={PAGE_SIZE}
          totalCount={totalCount}
          onPageChange={setPage}
        />
      )}

      <CallHistoryDetailDialog
        call={selectedCall}
        calls={allCalls ?? calls}
        onJumpToCall={jumpToCall}
        onOpenChange={closeDetailDialog}
      />
    </div>
  );
}
