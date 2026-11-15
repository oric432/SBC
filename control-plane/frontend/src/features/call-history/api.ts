import { createApi, fetchBaseQuery } from "@reduxjs/toolkit/query/react";

import { formatCodec } from "@/features/call-history/format";
import type { CallHistoryQueryArgs, CallHistoryResult, CallRecord } from "@/features/call-history/types";
import { unwrap, type ApiResponse } from "@/lib/api";

// Polled rather than pushed, same reasoning as registrations: there's no
// browser-facing websocket in this project.
export const CALL_HISTORY_POLL_INTERVAL_MS = 5000;

type CallRecordDto = Omit<CallRecord, "codec"> & { codec: string | null };
interface CallHistoryDto {
    rows: CallRecordDto[];
    totalCount: number;
}

const toCallRecord = (dto: CallRecordDto): CallRecord => ({ ...dto, codec: formatCodec(dto.codec) });

const toCallHistoryResult = (response: CallHistoryDto): CallHistoryResult => ({
    rows: response.rows.map(toCallRecord),
    totalCount: response.totalCount,
});

export const callHistoryApi = createApi({
    reducerPath: "callHistoryApi",
    baseQuery: fetchBaseQuery({ baseUrl: "/api/calls" }),
    tagTypes: ["CallHistory"],
    endpoints: (builder) => ({
        getCallHistory: builder.query<CallHistoryResult, CallHistoryQueryArgs>({
            query: ({ statuses, sortField, sortDir, page, pageSize }) => {
                const params = new URLSearchParams({ sortField, sortDir, page: String(page), pageSize: String(pageSize) });
                statuses.forEach((status) => params.append("status", status));
                return `?${params}`;
            },
            transformResponse: (response: ApiResponse<CallHistoryDto>) =>
                toCallHistoryResult(unwrap(response)),
            providesTags: [{ type: "CallHistory", id: "LIST" }],
        }),
        // Only feeds the unread/"New" count for the sidebar badge, independent
        // of whatever page/filter the table is on. The newest 100 calls (the
        // backend's page-size cap) is enough: anything older than that has
        // long been seen.
        getAllCalls: builder.query<CallRecord[], void>({
            query: () => "?sortField=timestamp&sortDir=desc&page=1&pageSize=100",
            transformResponse: (response: ApiResponse<CallHistoryDto>) =>
                toCallHistoryResult(unwrap(response)).rows,
            providesTags: [{ type: "CallHistory", id: "LIST" }],
        }),
    }),
});

export const { useGetCallHistoryQuery, useGetAllCallsQuery } = callHistoryApi;
