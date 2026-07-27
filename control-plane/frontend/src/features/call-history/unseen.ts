import { useCallback, useEffect, useMemo, useState, useSyncExternalStore } from "react";

import { useGetAllCallsQuery } from "@/features/call-history/api";

const LAST_SEEN_KEY = "sbc.callHistory.lastSeenAt";

type Listener = () => void;
const listeners = new Set<Listener>();

function getLastSeenAt(): string | null {
    return localStorage.getItem(LAST_SEEN_KEY);
}

// The only writer of the checkpoint — notifies subscribers synchronously so
// any component reading it via useSyncExternalStore re-renders immediately,
// with no reliance on effect ordering between components.
function setLastSeenAt(iso: string): void {
    localStorage.setItem(LAST_SEEN_KEY, iso);
    listeners.forEach((listener) => listener());
}

function subscribe(listener: Listener): () => void {
    listeners.add(listener);
    return () => listeners.delete(listener);
}

// Captures the checkpoint once on mount (before advancing it), so a row's
// "New" status stays stable for the rest of this visit even though the next
// visit won't flag it again. `null` means no prior checkpoint (first-ever
// visit) — callers should treat that as "nothing is new" rather than
// flagging the entire history.
//
// The read and the write are deliberately split: the lazy useState
// initializer only reads (a pure, idempotent operation — safe if React
// double-invokes it, e.g. under StrictMode in dev), while advancing the
// checkpoint is a side effect that runs separately in an effect. Reading
// AND writing inside the same initializer would make it impure — a
// StrictMode double-invoke would then have the second call read back the
// checkpoint the first call just wrote, collapsing the captured value to
// "now" and making every row appear already-seen.
//
// Also returns markAllAsRead(), which advances the checkpoint AND updates
// the captured value immediately — unlike the background auto-advance on
// mount, this clears "New" flags in the current view right away rather than
// only affecting the next visit.
export function useLastSeenCheckpoint(): [string | null, () => void] {
    const [capturedLastSeenAt, setCapturedLastSeenAt] = useState(getLastSeenAt);

    useEffect(() => {
        setLastSeenAt(new Date().toISOString());
    }, []);

    const markAllAsRead = useCallback(() => {
        const now = new Date().toISOString();
        setLastSeenAt(now);
        setCapturedLastSeenAt(now);
    }, []);

    return [capturedLastSeenAt, markAllAsRead];
}

export function isCallNew(call: { timestamp: string }, lastSeenAt: string | null): boolean {
    return lastSeenAt !== null && call.timestamp > lastSeenAt;
}

// Sidebar badge — reads the checkpoint via useSyncExternalStore (the
// React-blessed way to read external mutable state) so it updates the
// instant the Call History page's effect advances the checkpoint, without
// depending on effect-firing order between sibling components. An earlier
// version re-read the checkpoint from a location-change effect instead;
// under StrictMode's dev-mode double-invoke of effects, that re-read could
// fire again *after* the page's checkpoint-advancing effect had already run
// once, capturing the just-advanced value and permanently zeroing the count.
export function useUnseenCallCount(): number {
    const { data: calls } = useGetAllCallsQuery();
    const lastSeenAt = useSyncExternalStore(subscribe, getLastSeenAt);

    return useMemo(() => (calls ?? []).filter((call) => isCallNew(call, lastSeenAt)).length, [calls, lastSeenAt]);
}
