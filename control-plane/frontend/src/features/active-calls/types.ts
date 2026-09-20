import type { Codec } from "@/features/call-history/types";

// Mirrors control-plane/backend's GET /api/calls/active row shape (calls
// rows with ended_at IS NULL) — no codegen step, update this by hand if it
// changes. route/codec/answeredAt stay null until the engine reports the call
// answered.
export interface ActiveCall {
    id: string;
    sipCallId: string;
    caller: string;
    callee: string;
    route: string | null;
    codec: Codec | null;
    startedAt: string;
    answeredAt: string | null;
    // Started long enough ago that its "terminated" event was probably lost
    // (engine crash) rather than the call still being live.
    stale: boolean;
}
