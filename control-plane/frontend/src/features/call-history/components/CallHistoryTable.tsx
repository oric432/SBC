import { formatDistanceToNow, format } from "date-fns";
import { ArrowDown, ArrowUp, GitMerge, PhoneOff } from "lucide-react";
import { memo } from "react";

import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import {
  Tooltip,
  TooltipContent,
  TooltipProvider,
  TooltipTrigger,
} from "@/components/ui/tooltip";
import {
  formatSipUri,
  STATUS_BADGE_STYLES,
} from "@/features/call-history/format";
import type {
  CallHistorySortField,
  CallRecord,
  SortDirection,
} from "@/features/call-history/types";
import { isCallNew } from "@/features/call-history/unseen";
import { cn } from "@/lib/utils";

interface CallHistoryTableProps {
  calls: CallRecord[];
  sortField: CallHistorySortField;
  sortDir: SortDirection;
  onSortChange: (field: CallHistorySortField) => void;
  onSelectCall: (call: CallRecord) => void;
  lastSeenAt: string | null;
  highlightedCallId?: string;
}

interface SortableHeadProps {
  field: CallHistorySortField;
  label: string;
  sortField: CallHistorySortField;
  sortDir: SortDirection;
  onSortChange: (field: CallHistorySortField) => void;
}

function SortableHead({
  field,
  label,
  sortField,
  sortDir,
  onSortChange,
}: SortableHeadProps) {
  const isActive = sortField === field;
  const SortIcon = sortDir === "asc" ? ArrowUp : ArrowDown;

  return (
    <TableHead>
      <Button
        variant="ghost"
        size="sm"
        className="-ml-3 h-8 px-3"
        onClick={() => onSortChange(field)}
        aria-label={`Sort by ${label}${isActive ? (sortDir === "asc" ? " descending" : " ascending") : ""}`}
      >
        {label}
        {isActive && <SortIcon className="ml-2 h-4 w-4" />}
      </Button>
    </TableHead>
  );
}

export const CallHistoryTable = memo(function CallHistoryTable({
  calls,
  sortField,
  sortDir,
  onSortChange,
  onSelectCall,
  lastSeenAt,
  highlightedCallId,
}: CallHistoryTableProps) {
  const displayedCalls = calls.flatMap((call, index) => {
    const peer = call.mediaPeerCallId
      ? calls.find((candidate) => candidate.sipCallId === call.mediaPeerCallId)
      : undefined;
    if (!peer) return [{ call, child: false }];
    if (calls.findIndex((candidate) => candidate.id === peer.id) < index)
      return [];
    return [
      { call, child: false },
      { call: peer, child: true },
    ];
  });
  if (calls.length === 0) {
    return (
      <div className="flex h-40 flex-col items-center justify-center gap-2 rounded-md border border-dashed text-muted-foreground">
        <PhoneOff className="h-6 w-6" />
        <p className="text-sm">No calls match the current filters.</p>
      </div>
    );
  }

  return (
    <TooltipProvider delayDuration={300}>
      <div className="overflow-x-auto rounded-md border">
        <Table>
          <TableHeader>
            <TableRow className="hover:bg-transparent">
              <SortableHead
                field="timestamp"
                label="Date & time"
                sortField={sortField}
                sortDir={sortDir}
                onSortChange={onSortChange}
              />
              <SortableHead
                field="caller"
                label="Caller"
                sortField={sortField}
                sortDir={sortDir}
                onSortChange={onSortChange}
              />
              <SortableHead
                field="callee"
                label="Callee"
                sortField={sortField}
                sortDir={sortDir}
                onSortChange={onSortChange}
              />
              <TableHead>Route</TableHead>
              <TableHead>Codec</TableHead>
              <TableHead>Status</TableHead>
              <TableHead>Media joined to</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {displayedCalls.map(({ call, child }) => {
              const isNew = isCallNew(call, lastSeenAt);
              const mediaPeer = call.mediaPeerCallId
                ? calls.find(
                    (candidate) => candidate.sipCallId === call.mediaPeerCallId,
                  )
                : undefined;
              return (
                <TableRow
                  key={call.id}
                  id={`call-${call.id}`}
                  className={cn(
                    "cursor-pointer",
                    isNew && "bg-primary/5 hover:bg-primary/10",
                    child && "bg-muted/30",
                    highlightedCallId === call.id &&
                      "bg-blue-500/15 ring-2 ring-inset ring-blue-400 animate-pulse",
                  )}
                  onClick={() => onSelectCall(call)}
                >
                  <TableCell className="text-sm">
                    <div className="flex items-center gap-2">
                      {isNew && (
                        <Badge className="border-transparent bg-primary/15 text-primary hover:bg-primary/15">
                          New
                        </Badge>
                      )}
                      <Tooltip>
                        <TooltipTrigger asChild>
                          <span>
                            {formatDistanceToNow(new Date(call.timestamp), {
                              addSuffix: true,
                            })}
                          </span>
                        </TooltipTrigger>
                        <TooltipContent>
                          {format(new Date(call.timestamp), "PPpp")}
                        </TooltipContent>
                      </Tooltip>
                    </div>
                  </TableCell>
                  <TableCell className="font-mono text-sm">
                    <div
                      className={
                        child
                          ? "flex items-center gap-2 border-l-2 border-blue-400 pl-3"
                          : undefined
                      }
                    >
                      {child && (
                        <Tooltip>
                          <TooltipTrigger asChild>
                            <GitMerge className="h-4 w-4 shrink-0 cursor-help text-blue-500" />
                          </TooltipTrigger>
                          <TooltipContent>
                            Earlier bridged call linked to the row above
                          </TooltipContent>
                        </Tooltip>
                      )}
                      {formatSipUri(call.caller)}
                    </div>
                  </TableCell>
                  <TableCell className="font-mono text-sm">
                    {formatSipUri(call.callee)}
                  </TableCell>
                  <TableCell className="max-w-[220px] font-mono text-sm">
                    {call.route ? (
                      <Tooltip>
                        <TooltipTrigger asChild>
                          <span className="block truncate">{call.route}</span>
                        </TooltipTrigger>
                        <TooltipContent className="font-mono">
                          {call.route}
                        </TooltipContent>
                      </Tooltip>
                    ) : (
                      "—"
                    )}
                  </TableCell>
                  <TableCell className="text-sm">{call.codec ?? "—"}</TableCell>
                  <TableCell>
                    <Badge className={cn(STATUS_BADGE_STYLES[call.status])}>
                      {call.status}
                    </Badge>
                  </TableCell>
                  <TableCell className="max-w-[220px] font-mono text-sm">
                    {mediaPeer && (
                      <Badge
                        variant="outline"
                        className="gap-1 border-blue-400/50 bg-blue-500/10 text-blue-700 dark:text-blue-300"
                      >
                        <GitMerge className="h-3 w-3" />
                        Bridged leg
                      </Badge>
                    )}
                  </TableCell>
                </TableRow>
              );
            })}
          </TableBody>
        </Table>
      </div>
    </TooltipProvider>
  );
});
