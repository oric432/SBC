import { format } from "date-fns";
import { ArrowRight, ExternalLink, GitMerge } from "lucide-react";
import type { ReactNode } from "react";

import { Badge } from "@/components/ui/badge";
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import {
  STATUS_BADGE_STYLES,
  formatDuration,
  formatSipUri,
} from "@/features/call-history/format";
import type { CallRecord } from "@/features/call-history/types";
import { cn } from "@/lib/utils";
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip";

interface CallHistoryDetailDialogProps {
  call: CallRecord | undefined;
  calls: CallRecord[];
  onOpenChange: (open: boolean) => void;
  onJumpToCall: (call: CallRecord) => void;
}

function DetailRow({ label, value }: { label: string; value: ReactNode }) {
  return (
    <div className="flex items-start justify-between gap-4 py-2 text-sm">
      <span className="text-muted-foreground">{label}</span>
      <span className="text-right font-medium">{value}</span>
    </div>
  );
}

export function CallHistoryDetailDialog({
  call,
  calls,
  onOpenChange,
  onJumpToCall,
}: CallHistoryDetailDialogProps) {
  const mediaPeer = call?.mediaPeerCallId
    ? calls.find((candidate) => candidate.sipCallId === call.mediaPeerCallId)
    : undefined;

  return (
    <Dialog open={Boolean(call)} onOpenChange={onOpenChange}>
      <DialogContent className="sm:max-w-2xl">
        <DialogHeader>
          <DialogTitle>Call details</DialogTitle>
        </DialogHeader>
        {call && (
          <div className="space-y-4">
            {mediaPeer && (
              <section className="rounded-lg border border-blue-400/40 bg-blue-500/5 p-4">
                <div className="mb-3 flex items-center gap-2 text-sm font-semibold">
                  <GitMerge className="h-4 w-4 text-blue-500" />
                  Call topology
                </div>
                <div className="grid gap-3 text-sm sm:grid-cols-[1fr_auto_1fr] sm:items-center">
                  <div className="rounded-md border bg-background p-3">
                    <p className="text-xs text-muted-foreground">This call</p>
                    <p className="font-medium">{formatSipUri(call.callee)}</p>
                    <p className="text-xs text-muted-foreground">
                      {call.codec ?? "Codec pending"} · {call.status}
                    </p>
                  </div>
                  <ArrowRight className="mx-auto h-5 w-5 text-blue-500" />
                  <Tooltip>
                    <TooltipTrigger asChild>
                      <button
                        type="button"
                        className="cursor-pointer rounded-md border bg-background p-3 text-left transition-colors hover:border-blue-400 hover:bg-blue-500/5"
                        onClick={() => onJumpToCall(mediaPeer)}
                      >
                        <p className="text-xs text-muted-foreground">
                          Bridged leg
                        </p>
                        <p className="font-medium">
                          {formatSipUri(mediaPeer.callee)}
                        </p>
                        <p className="text-xs text-muted-foreground">
                          {mediaPeer.codec ?? "Codec pending"} ·{" "}
                          {mediaPeer.status}
                        </p>
                        <p className="mt-2 flex items-center gap-1 text-xs text-blue-600 dark:text-blue-300">
                          <ExternalLink className="h-3 w-3" />
                          Inspect bridged session
                        </p>
                      </button>
                    </TooltipTrigger>
                    <TooltipContent>
                      Click to inspect bridged session
                    </TooltipContent>
                  </Tooltip>
                </div>
                <div className="mt-3 grid gap-2 text-xs text-muted-foreground sm:grid-cols-2">
                  <span>
                    This call started {format(new Date(call.timestamp), "PPp")}
                  </span>
                  <span>
                    Peer started {format(new Date(mediaPeer.timestamp), "PPp")}
                  </span>
                </div>
              </section>
            )}
            <div className="divide-y">
              <DetailRow
                label="Caller"
                value={<span className="font-mono">{call.caller}</span>}
              />
              <DetailRow
                label="Callee"
                value={<span className="font-mono">{call.callee}</span>}
              />
              <DetailRow
                label="Route"
                value={<span className="font-mono">{call.route ?? "—"}</span>}
              />
              <DetailRow label="Codec" value={call.codec ?? "—"} />
              <DetailRow
                label="Status"
                value={
                  <Badge className={cn(STATUS_BADGE_STYLES[call.status])}>
                    {call.status}
                  </Badge>
                }
              />
              <DetailRow
                label="Date & time"
                value={format(new Date(call.timestamp), "PPpp")}
              />
              {call.durationSeconds !== undefined && (
                <DetailRow
                  label="Duration"
                  value={formatDuration(call.durationSeconds)}
                />
              )}
              <DetailRow
                label="SIP Call-ID"
                value={
                  <span className="font-mono text-xs">{call.sipCallId}</span>
                }
              />
              {call.failureReason && (
                <DetailRow label="Failure reason" value={call.failureReason} />
              )}
            </div>
          </div>
        )}
      </DialogContent>
    </Dialog>
  );
}
