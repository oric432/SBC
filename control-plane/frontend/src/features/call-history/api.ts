import { createApi, fakeBaseQuery } from "@reduxjs/toolkit/query/react";

import { DUMMY_CALL_HISTORY } from "@/features/call-history/data/dummyCallHistory";
import type { CallHistoryQueryArgs, CallHistoryResult, CallRecord } from "@/features/call-history/types";

// Filters, sorts, and paginates the dummy dataset the same way a real backend
// list endpoint would, so swapping this queryFn for a real `query` later is
// the only change needed — components/hooks stay the same.
function resolveCallHistory(args: CallHistoryQueryArgs): CallHistoryResult {
    const { statuses, sortField, sortDir, page, pageSize } = args;

    const filtered =
        statuses.length === 0 ? DUMMY_CALL_HISTORY : DUMMY_CALL_HISTORY.filter((call) => statuses.includes(call.status));

    const compare = (a: CallRecord, b: CallRecord): number => {
        if (sortField === "timestamp") return a.timestamp.localeCompare(b.timestamp);
        return a[sortField].localeCompare(b[sortField]);
    };

    const sorted = filtered.toSorted((a, b) => (sortDir === "asc" ? compare(a, b) : compare(b, a)));

    const start = (page - 1) * pageSize;
    const rows = sorted.slice(start, start + pageSize);

    return { rows, totalCount: sorted.length };
}

export const callHistoryApi = createApi({
    reducerPath: "callHistoryApi",
    baseQuery: fakeBaseQuery(),
    tagTypes: ["CallHistory"],
    endpoints: (builder) => ({
        getCallHistory: builder.query<CallHistoryResult, CallHistoryQueryArgs>({
            queryFn: (args) => ({ data: resolveCallHistory(args) }),
            providesTags: [{ type: "CallHistory", id: "LIST" }],
        }),
        // Full, unpaginated list — used only to compute the unread/"New" count
        // for the sidebar badge, independent of whatever page/filter the
        // table happens to be on.
        getAllCalls: builder.query<CallRecord[], void>({
            queryFn: () => ({ data: [...DUMMY_CALL_HISTORY] }),
            providesTags: [{ type: "CallHistory", id: "LIST" }],
        }),
    }),
});

export const { useGetCallHistoryQuery, useGetAllCallsQuery } = callHistoryApi;
