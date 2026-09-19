import { createApi, fetchBaseQuery } from "@reduxjs/toolkit/query/react";
import { unwrap } from "@/lib/api";
import type { Registration } from "./types";

// Polled rather than pushed: the control plane has no browser-facing
// websocket today (only the engine-facing channel in ws/engineChannel.ts),
// and this list changing a few seconds late is imperceptible here --
// building a second live-push path just for this isn't worth it yet.
export const REGISTRATIONS_POLL_INTERVAL_MS = 5000;

export const registrationsApi = createApi({
    reducerPath: "registrationsApi",
    baseQuery: fetchBaseQuery({ baseUrl: "/api/registrations" }),
    tagTypes: ["Registration"],
    endpoints: (builder) => ({
        getRegistrations: builder.query<Registration[], void>({
            query: () => "",
            transformResponse: unwrap<Registration[]>,
            providesTags: [{ type: "Registration", id: "LIST" }],
        }),
    }),
});

export const { useGetRegistrationsQuery } = registrationsApi;
