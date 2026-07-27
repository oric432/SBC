import { configureStore } from "@reduxjs/toolkit";
import { callHistoryApi } from "@/features/call-history/api";
import { routesApi } from "@/features/routes/api";

export const store = configureStore({
    reducer: {
        [routesApi.reducerPath]: routesApi.reducer,
        [callHistoryApi.reducerPath]: callHistoryApi.reducer,
    },
    middleware: (getDefaultMiddleware) => getDefaultMiddleware().concat(routesApi.middleware, callHistoryApi.middleware),
});

export type RootState = ReturnType<typeof store.getState>;
export type AppDispatch = typeof store.dispatch;
