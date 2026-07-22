import { createApi, fetchBaseQuery } from "@reduxjs/toolkit/query/react";
import type { ApiResponse } from "@/lib/api";
import type { CreateRoutePayload, RouteRule, RouteSnapshot, UpdateRoutePayload } from "./types";

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
                          ...Object.keys(result.routes).map((routeKey) => ({ type: "Route" as const, id: routeKey })),
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
            query: ({ route_key, ...body }) => ({
                url: `/routes/${route_key}`,
                method: "PUT",
                body,
            }),
            transformResponse: unwrap<RouteRule>,
            invalidatesTags: (_result, _error, { route_key }) => [{ type: "Route", id: route_key }],
        }),
        deleteRoute: builder.mutation<void, string>({
            query: (routeKey) => ({
                url: `/routes/${routeKey}`,
                method: "DELETE",
            }),
            transformResponse: unwrap<void>,
            invalidatesTags: (_result, _error, routeKey) => [{ type: "Route", id: routeKey }],
        }),
    }),
});

export const { useGetRoutesQuery, useCreateRouteMutation, useUpdateRouteMutation, useDeleteRouteMutation } = routesApi;
