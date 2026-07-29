import { createApi, fetchBaseQuery } from "@reduxjs/toolkit/query/react";
import type { ApiResponse } from "@/lib/api";
import type { CreateRoutePayload, RouteRule, RouteSnapshot, SwapRoutePayload, UpdateRoutePayload } from "./types";

// 2xx responses are always { success: true, data }; fetchBaseQuery routes
// non-2xx responses to transformErrorResponse instead, so this only ever
// sees the success shape. Unwrap it so the rest of the app works with the
// payload type directly.
const unwrap = <T>(response: ApiResponse<T>): T => (response as { data: T }).data;

export const routesApi = createApi({
    reducerPath: "routesApi",
    baseQuery: fetchBaseQuery({ baseUrl: "/api/b2bua" }),
    tagTypes: ["Route"],
    endpoints: (builder) => ({
        getRoutes: builder.query<RouteSnapshot, void>({
            query: () => "/routes",
            transformResponse: unwrap<RouteSnapshot>,
            providesTags: (result) =>
                result
                    ? [
                          ...Object.keys(result.routes).map((priority) => ({ type: "Route" as const, id: priority })),
                          { type: "Route" as const, id: "LIST" },
                      ]
                    : [{ type: "Route" as const, id: "LIST" }],
        }),
        createRoute: builder.mutation<RouteRule, CreateRoutePayload>({
            query: (payload) => ({
                url: "/routes",
                method: "POST",
                body: payload,
            }),
            transformResponse: unwrap<RouteRule>,
            invalidatesTags: [{ type: "Route", id: "LIST" }],
        }),
        updateRoute: builder.mutation<RouteRule, UpdateRoutePayload>({
            query: ({ currentPriority, ...body }) => ({
                url: `/routes/${currentPriority}`,
                method: "PUT",
                body,
            }),
            transformResponse: unwrap<RouteRule>,
            invalidatesTags: (_result, _error, { currentPriority }) => [
                { type: "Route", id: String(currentPriority) },
                { type: "Route", id: "LIST" },
            ],
        }),
        swapRoute: builder.mutation<RouteRule, SwapRoutePayload>({
            query: ({ currentPriority, targetPriority, uri, sip_address, port, codec }) => ({
                url: `/routes/${currentPriority}/swap`,
                method: "PATCH",
                body: { targetPriority, uri, sip_address, port, codec },
            }),
            transformResponse: unwrap<RouteRule>,
            invalidatesTags: [{ type: "Route", id: "LIST" }],
        }),
        deleteRoute: builder.mutation<void, number>({
            query: (priority) => ({
                url: `/routes/${priority}`,
                method: "DELETE",
            }),
            transformResponse: unwrap<void>,
            invalidatesTags: (_result, _error, priority) => [
                { type: "Route", id: String(priority) },
                { type: "Route", id: "LIST" },
            ],
        }),
    }),
});

export const {
    useGetRoutesQuery,
    useCreateRouteMutation,
    useUpdateRouteMutation,
    useSwapRouteMutation,
    useDeleteRouteMutation,
} = routesApi;
