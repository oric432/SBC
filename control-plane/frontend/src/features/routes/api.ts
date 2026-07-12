import { createApi, fetchBaseQuery } from "@reduxjs/toolkit/query/react";
import type { CreateRoutePayload, RouteRule, RouteSnapshot, UpdateRoutePayload } from "./types";

export const routesApi = createApi({
    reducerPath: "routesApi",
    baseQuery: fetchBaseQuery({ baseUrl: "/api/b2bua" }),
    tagTypes: ["Route"],
    endpoints: (builder) => ({
        getRoutes: builder.query<RouteSnapshot, void>({
            query: () => "/routes",
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
            invalidatesTags: [{ type: "Route", id: "LIST" }],
        }),
        updateRoute: builder.mutation<RouteRule, UpdateRoutePayload>({
            query: ({ route_key, ...body }) => ({
                url: `/routes/${route_key}`,
                method: "PUT",
                body,
            }),
            invalidatesTags: (_result, _error, { route_key }) => [{ type: "Route", id: route_key }],
        }),
        deleteRoute: builder.mutation<void, string>({
            query: (routeKey) => ({
                url: `/routes/${routeKey}`,
                method: "DELETE",
            }),
            invalidatesTags: (_result, _error, routeKey) => [{ type: "Route", id: routeKey }],
        }),
    }),
});

export const { useGetRoutesQuery, useCreateRouteMutation, useUpdateRouteMutation, useDeleteRouteMutation } = routesApi;
