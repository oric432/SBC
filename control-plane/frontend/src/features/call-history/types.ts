export type CallStatus = "Success" | "Failed" | "No Route Available" | "Blocked";

export const CALL_STATUSES: CallStatus[] = ["Success", "Failed", "No Route Available", "Blocked"];

export type Codec = "G.711 (PCMU)" | "G.711 (PCMA)" | "G.729" | "Opus" | "G.722";

export interface CallRecord {
    id: string;
    caller: string;
    callee: string;
    route: string | null;
    codec: Codec;
    status: CallStatus;
    timestamp: string;
    durationSeconds?: number;
    sipCallId: string;
    failureReason?: string;
}

export type CallHistorySortField = "timestamp" | "caller" | "callee";
export type SortDirection = "asc" | "desc";

export interface CallHistoryQueryArgs {
    statuses: CallStatus[];
    sortField: CallHistorySortField;
    sortDir: SortDirection;
    page: number;
    pageSize: number;
}

export interface CallHistoryResult {
    rows: CallRecord[];
    totalCount: number;
}
