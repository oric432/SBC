import { createApi, fetchBaseQuery } from "@reduxjs/toolkit/query/react";

import { formatCodec } from "@/features/call-history/format";
import { unwrap, type ApiResponse } from "@/lib/api";
import type { ActiveCall } from "./types";

// Polled rather than pushed, same reasoning as registrations: there's no
// browser-facing websocket in this project.
export const ACTIVE_CALLS_POLL_INTERVAL_MS = 5000;

type ActiveCallDto = Omit<ActiveCall, "codec"> & { codec: string | null };

export const activeCallsApi = createApi({
    reducerPath: "activeCallsApi",
    baseQuery: fetchBaseQuery({ baseUrl: "/api/calls/active" }),
    endpoints: (builder) => ({
        getActiveCalls: builder.query<ActiveCall[], void>({
            query: () => "",
            transformResponse: (response: ApiResponse<ActiveCallDto[]>) =>
                unwrap(response).map((call) => ({ ...call, codec: formatCodec(call.codec) })),
        }),
    }),
});

export const { useGetActiveCallsQuery } = activeCallsApi;
