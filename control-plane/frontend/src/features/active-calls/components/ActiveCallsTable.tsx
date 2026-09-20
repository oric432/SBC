import { PhoneOff } from "lucide-react";
import { memo, useEffect, useState } from "react";

import { Badge } from "@/components/ui/badge";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Tooltip, TooltipContent, TooltipProvider, TooltipTrigger } from "@/components/ui/tooltip";
import type { ActiveCall } from "@/features/active-calls/types";
import { formatDuration } from "@/features/call-history/format";

interface ActiveCallsTableProps {
    calls: ActiveCall[];
}

// Re-renders the table once a second so durations tick between the 5s polls
// -- computed off the engine-reported answeredAt, not a browser-side timer
// started when a row first appeared.
function useNow(intervalMs: number): number {
    const [now, setNow] = useState(() => Date.now());
    useEffect(() => {
        const id = setInterval(() => setNow(Date.now()), intervalMs);
        return () => clearInterval(id);
    }, [intervalMs]);
    return now;
}

function StateCell({ call }: { call: ActiveCall }) {
    if (call.stale) {
        return (
            <Tooltip>
                <TooltipTrigger asChild>
                    <Badge variant="secondary">Possibly stale</Badge>
                </TooltipTrigger>
                <TooltipContent>
                    No end-of-call event was ever received; the engine may have stopped mid-call.
                </TooltipContent>
            </Tooltip>
        );
    }
    return call.answeredAt ? (
        <Badge className="border-transparent bg-emerald-500/15 text-emerald-600 hover:bg-emerald-500/15 dark:text-emerald-400">
            In call
        </Badge>
    ) : (
        <Badge className="border-transparent bg-amber-500/15 text-amber-600 hover:bg-amber-500/15 dark:text-amber-400">
            Setting up
        </Badge>
    );
}

export const ActiveCallsTable = memo(function ActiveCallsTable({ calls }: ActiveCallsTableProps) {
    const now = useNow(1000);

    if (calls.length === 0) {
        return (
            <div className="flex h-40 flex-col items-center justify-center gap-2 rounded-md border border-dashed text-muted-foreground">
                <PhoneOff className="h-6 w-6" />
                <p className="text-sm">No calls in progress.</p>
            </div>
        );
    }

    return (
        <TooltipProvider delayDuration={300}>
            <div className="overflow-x-auto rounded-md border">
                <Table>
                    <TableHeader>
                        <TableRow className="hover:bg-transparent">
                            <TableHead>Caller</TableHead>
                            <TableHead>Callee</TableHead>
                            <TableHead>Route</TableHead>
                            <TableHead>Codec</TableHead>
                            <TableHead>State</TableHead>
                            <TableHead>Duration</TableHead>
                        </TableRow>
                    </TableHeader>
                    <TableBody>
                        {calls.map((call) => (
                            <TableRow key={call.id}>
                                <TableCell className="font-mono text-sm">{call.caller}</TableCell>
                                <TableCell className="font-mono text-sm">{call.callee}</TableCell>
                                <TableCell className="max-w-[220px] font-mono text-sm">
                                    {call.route ? (
                                        <Tooltip>
                                            <TooltipTrigger asChild>
                                                <span className="block truncate">{call.route}</span>
                                            </TooltipTrigger>
                                            <TooltipContent className="font-mono">{call.route}</TooltipContent>
                                        </Tooltip>
                                    ) : (
                                        "—"
                                    )}
                                </TableCell>
                                <TableCell className="text-sm">{call.codec ?? "—"}</TableCell>
                                <TableCell>
                                    <StateCell call={call} />
                                </TableCell>
                                <TableCell className="font-mono text-sm tabular-nums">
                                    {call.answeredAt
                                        ? formatDuration(
                                              Math.max(0, Math.floor((now - new Date(call.answeredAt).getTime()) / 1000)),
                                          )
                                        : "—"}
                                </TableCell>
                            </TableRow>
                        ))}
                    </TableBody>
                </Table>
            </div>
        </TooltipProvider>
    );
});
