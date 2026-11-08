import { format } from "date-fns";
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
} from "@/features/call-history/format";
import type { CallRecord } from "@/features/call-history/types";
import { cn } from "@/lib/utils";

interface CallHistoryDetailDialogProps {
  call: CallRecord | undefined;
  calls: CallRecord[];
  onSelectCall: (call: CallRecord) => void;
  onOpenChange: (open: boolean) => void;
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
  onSelectCall,
  onOpenChange,
}: CallHistoryDetailDialogProps) {
  const mediaPeer = call?.mediaPeerCallId
    ? calls.find((candidate) => candidate.sipCallId === call.mediaPeerCallId)
    : undefined;

  return (
    <Dialog open={Boolean(call)} onOpenChange={onOpenChange}>
      <DialogContent className="sm:max-w-md">
        <DialogHeader>
          <DialogTitle>Call details</DialogTitle>
        </DialogHeader>
        {call && (
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
            {call.mediaPeerCallId && (
              <DetailRow
                label="Media joined with"
                value={
                  mediaPeer ? (
                    <button
                      type="button"
                      className="font-mono text-xs text-primary underline-offset-4 hover:underline"
                      onClick={() => onSelectCall(mediaPeer)}
                    >
                      {call.mediaPeerCallId}
                    </button>
                  ) : (
                    <span className="font-mono text-xs">
                      {call.mediaPeerCallId}
                    </span>
                  )
                }
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
        )}
      </DialogContent>
    </Dialog>
  );
}
