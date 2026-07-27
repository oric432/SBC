import type { CallStatus } from "@/features/call-history/types";

export const STATUS_BADGE_STYLES: Record<CallStatus, string> = {
    Success: "border-transparent bg-emerald-500/15 text-emerald-600 dark:text-emerald-400 hover:bg-emerald-500/15",
    Failed: "border-transparent bg-destructive/15 text-destructive hover:bg-destructive/15",
    "Invalid Route": "border-transparent bg-amber-500/15 text-amber-600 dark:text-amber-400 hover:bg-amber-500/15",
    Blocked: "border-transparent bg-destructive/15 text-destructive hover:bg-destructive/15",
};

export function formatDuration(durationSeconds: number): string {
    const minutes = Math.floor(durationSeconds / 60);
    const seconds = durationSeconds % 60;
    return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}
