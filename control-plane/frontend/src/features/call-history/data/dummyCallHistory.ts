import type { CallRecord, CallStatus, Codec } from "@/features/call-history/types";

const CALLER_NUMBERS = [
    "+14155550101",
    "+14155550142",
    "+12065550110",
    "+12065550187",
    "+13105550199",
    "+13105550123",
    "+16465550156",
    "+16465550178",
];

const CALLEE_NUMBERS = [
    "+442071838750",
    "+493012345678",
    "+33142681500",
    "+81312345678",
    "+61293744000",
    "+12125550134",
    "+12125550165",
    "+18005550190",
];

const ROUTE_URIS = [
    "sip:trunk-a@203.0.113.10:5060",
    "sip:trunk-b@203.0.113.20:5060",
    "sip:carrier-eu@198.51.100.5:5060",
    "sip:carrier-apac@198.51.100.15:5060",
];

const CODECS: Codec[] = ["G.711 (PCMU)", "G.711 (PCMA)", "G.729", "Opus", "G.722"];

// Weighted so "Success" dominates, matching a realistic call-history distribution.
const STATUS_CYCLE: CallStatus[] = [
    "Success",
    "Success",
    "Success",
    "Failed",
    "Success",
    "No Route Available",
    "Success",
    "Blocked",
    "Success",
    "Failed",
];

const FAILURE_REASONS: Record<Exclude<CallStatus, "Success">, string> = {
    Failed: "Callee did not answer within timeout",
    "No Route Available": "No matching route found for callee prefix",
    Blocked: "Caller number matched the block list",
};

const RECORD_COUNT = 50;

// Deterministic (no Math.random) so the dummy list is stable across reloads.
function buildDummyCallHistory(): CallRecord[] {
    const now = Date.now();
    const records: CallRecord[] = [];

    for (let i = 0; i < RECORD_COUNT; i += 1) {
        const status = STATUS_CYCLE[i % STATUS_CYCLE.length];
        const hasRoute = status === "Success" || status === "Failed";
        const timestamp = new Date(now - i * 27 * 60 * 1000).toISOString();

        records.push({
            id: `call-${i + 1}`,
            caller: CALLER_NUMBERS[i % CALLER_NUMBERS.length],
            callee: CALLEE_NUMBERS[i % CALLEE_NUMBERS.length],
            route: hasRoute ? ROUTE_URIS[i % ROUTE_URIS.length] : null,
            codec: CODECS[i % CODECS.length],
            status,
            timestamp,
            durationSeconds: status === "Success" ? 15 + ((i * 37) % 540) : undefined,
            sipCallId: `${(1000 + i * 7).toString(16)}-${i}@sbc.local`,
            failureReason: status === "Success" ? undefined : FAILURE_REASONS[status],
        });
    }

    return records;
}

export const DUMMY_CALL_HISTORY: CallRecord[] = buildDummyCallHistory();
