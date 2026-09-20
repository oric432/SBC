import { AlertTriangle, PhoneCall } from "lucide-react";

import { Skeleton } from "@/components/ui/skeleton";
import { ACTIVE_CALLS_POLL_INTERVAL_MS, useGetActiveCallsQuery } from "@/features/active-calls/api";
import { ActiveCallsTable } from "@/features/active-calls/components/ActiveCallsTable";
import { getApiErrorMessage } from "@/lib/api";

export function ActiveCallsPage() {
    const { data, isLoading, isError, error } = useGetActiveCallsQuery(undefined, {
        pollingInterval: ACTIVE_CALLS_POLL_INTERVAL_MS,
    });

    const calls = data ?? [];

    return (
        <div className="space-y-6">
            <div className="flex items-center gap-3">
                <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-lg bg-primary/10 text-primary">
                    <PhoneCall className="h-5 w-5" />
                </div>
                <div>
                    <h1 className="text-2xl font-semibold tracking-tight">Active Calls</h1>
                    <p className="text-sm text-muted-foreground">Calls currently being set up or in progress on the B2BUA.</p>
                </div>
            </div>

            {!isLoading && !isError && (
                <div className="flex items-center gap-2 text-sm text-muted-foreground">
                    <span className="font-mono tabular-nums text-foreground">{calls.length}</span>
                    {calls.length === 1 ? "call in progress" : "calls in progress"}
                </div>
            )}

            {isLoading ? (
                <div className="space-y-2 rounded-md border p-2">
                    <Skeleton className="h-8 w-full" />
                    <Skeleton className="h-10 w-full" />
                    <Skeleton className="h-10 w-full" />
                </div>
            ) : isError ? (
                <div className="flex h-32 flex-col items-center justify-center gap-2 rounded-md border border-dashed text-sm text-destructive">
                    <AlertTriangle className="h-5 w-5" />
                    {getApiErrorMessage(error)}
                </div>
            ) : (
                <ActiveCallsTable calls={calls} />
            )}
        </div>
    );
}
