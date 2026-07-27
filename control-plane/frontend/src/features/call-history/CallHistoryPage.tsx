import { CheckCheck, History } from "lucide-react";
import { useCallback, useMemo, useState } from "react";

import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { useGetAllCallsQuery, useGetCallHistoryQuery } from "@/features/call-history/api";
import { CallHistoryDetailDialog } from "@/features/call-history/components/CallHistoryDetailDialog";
import { CallHistoryPagination } from "@/features/call-history/components/CallHistoryPagination";
import { CallHistoryStatusFilter } from "@/features/call-history/components/CallHistoryStatusFilter";
import { CallHistoryTable } from "@/features/call-history/components/CallHistoryTable";
import type { CallHistorySortField, CallRecord, CallStatus, SortDirection } from "@/features/call-history/types";
import { isCallNew, useLastSeenCheckpoint } from "@/features/call-history/unseen";

const PAGE_SIZE = 10;

export function CallHistoryPage() {
    const [statuses, setStatuses] = useState<CallStatus[]>([]);
    const [sortField, setSortField] = useState<CallHistorySortField>("timestamp");
    const [sortDir, setSortDir] = useState<SortDirection>("desc");
    const [page, setPage] = useState(1);
    const [selectedCall, setSelectedCall] = useState<CallRecord | undefined>(undefined);
    const [lastSeenAt, markAllAsRead] = useLastSeenCheckpoint();
    const { data: allCalls } = useGetAllCallsQuery();
    const hasUnread = useMemo(() => (allCalls ?? []).some((call) => isCallNew(call, lastSeenAt)), [allCalls, lastSeenAt]);

    const { data, isLoading } = useGetCallHistoryQuery({
        statuses,
        sortField,
        sortDir,
        page,
        pageSize: PAGE_SIZE,
    });

    const handleStatusChange = useCallback((next: CallStatus[]) => {
        setStatuses(next);
        setPage(1);
    }, []);

    const handleSortChange = useCallback(
        (field: CallHistorySortField) => {
            setSortField(field);
            setSortDir((prevDir) => (field === sortField ? (prevDir === "asc" ? "desc" : "asc") : "desc"));
            setPage(1);
        },
        [sortField],
    );

    const closeDetailDialog = useCallback((open: boolean) => {
        if (!open) setSelectedCall(undefined);
    }, []);

    const calls = data?.rows ?? [];
    const totalCount = data?.totalCount ?? 0;

    return (
        <div className="space-y-6">
            <div className="flex items-start justify-between gap-4">
                <div className="flex items-center gap-3">
                    <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-lg bg-primary/10 text-primary">
                        <History className="h-5 w-5" />
                    </div>
                    <div>
                        <h1 className="text-2xl font-semibold tracking-tight">Call History</h1>
                        <p className="text-sm text-muted-foreground">Past call attempts handled by the B2BUA.</p>
                    </div>
                </div>
                <CallHistoryStatusFilter selected={statuses} onChange={handleStatusChange} />
            </div>

            {!isLoading && (
                <div className="flex items-center justify-between gap-2">
                    <div className="flex items-center gap-2 text-sm text-muted-foreground">
                        <span className="font-mono tabular-nums text-foreground">{totalCount}</span>
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
            ) : (
                <CallHistoryTable
                    calls={calls}
                    sortField={sortField}
                    sortDir={sortDir}
                    onSortChange={handleSortChange}
                    onSelectCall={setSelectedCall}
                    lastSeenAt={lastSeenAt}
                />
            )}

            {!isLoading && totalCount > 0 && (
                <CallHistoryPagination page={page} pageSize={PAGE_SIZE} totalCount={totalCount} onPageChange={setPage} />
            )}

            <CallHistoryDetailDialog call={selectedCall} onOpenChange={closeDetailDialog} />
        </div>
    );
}
