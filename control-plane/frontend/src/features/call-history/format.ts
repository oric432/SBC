import type { CallStatus, Codec } from "@/features/call-history/types";

export const STATUS_BADGE_STYLES: Record<CallStatus, string> = {
    Success: "border-transparent bg-emerald-500/15 text-emerald-600 dark:text-emerald-400 hover:bg-emerald-500/15",
    Failed: "border-transparent bg-destructive/15 text-destructive hover:bg-destructive/15",
    "No Route Available": "border-transparent bg-amber-500/15 text-amber-600 dark:text-amber-400 hover:bg-amber-500/15",
    Blocked: "border-transparent bg-destructive/15 text-destructive hover:bg-destructive/15",
};

export function formatDuration(durationSeconds: number): string {
    const minutes = Math.floor(durationSeconds / 60);
    const seconds = durationSeconds % 60;
    return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}

const CODEC_LABELS: Record<string, Codec> = {
    PCMU: "G.711 (PCMU)",
    PCMA: "G.711 (PCMA)",
    G722: "G.722",
};

// The engine reports codecs by their SDP names; the UI shows friendly labels.
// A name this map doesn't know is shown as-is rather than hidden.
export function formatCodec(name: string | null): Codec | null {
    if (name === null) return null;
    return CODEC_LABELS[name] ?? (name as Codec);
}
