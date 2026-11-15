import { createApi, fetchBaseQuery } from "@reduxjs/toolkit/query/react";
import { unwrap } from "@/lib/api";
import type {
    CreateSipUserPayload,
    SipUser,
    UpdateSipUserPayload,
} from "./types";

export const sipUsersApi = createApi({
    reducerPath: "sipUsersApi",
    baseQuery: fetchBaseQuery({ baseUrl: "/api/sip-users" }),
    tagTypes: ["SipUser"],
    endpoints: (builder) => ({
        getSipUsers: builder.query<SipUser[], void>({
            query: () => "",
            transformResponse: unwrap<SipUser[]>,
            providesTags: (result) =>
                result
                    ? [
                          ...result.map((user) => ({
                              type: "SipUser" as const,
                              id: user.id,
                          })),
                          { type: "SipUser" as const, id: "LIST" },
                      ]
                    : [{ type: "SipUser" as const, id: "LIST" }],
        }),
        createSipUser: builder.mutation<SipUser, CreateSipUserPayload>({
            query: (payload) => ({
                url: "",
                method: "POST",
                body: payload,
            }),
            transformResponse: unwrap<SipUser>,
            invalidatesTags: [{ type: "SipUser", id: "LIST" }],
        }),
        updateSipUser: builder.mutation<SipUser, UpdateSipUserPayload>({
            query: ({ id, ...body }) => ({
                url: `/${id}`,
                method: "PUT",
                body,
            }),
            transformResponse: unwrap<SipUser>,
            invalidatesTags: (_result, _error, { id }) => [
                { type: "SipUser", id },
                { type: "SipUser", id: "LIST" },
            ],
        }),
        deleteSipUser: builder.mutation<void, number>({
            query: (id) => ({
                url: `/${id}`,
                method: "DELETE",
            }),
            transformResponse: unwrap<void>,
            invalidatesTags: (_result, _error, id) => [
                { type: "SipUser", id },
                { type: "SipUser", id: "LIST" },
            ],
        }),
    }),
});

export const {
    useGetSipUsersQuery,
    useCreateSipUserMutation,
    useUpdateSipUserMutation,
    useDeleteSipUserMutation,
} = sipUsersApi;
