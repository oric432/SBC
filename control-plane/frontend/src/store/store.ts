import { configureStore } from "@reduxjs/toolkit";
import { activeCallsApi } from "@/features/active-calls/api";
import { callHistoryApi } from "@/features/call-history/api";
import { registrationsApi } from "@/features/registrations/api";
import { routesApi } from "@/features/routes/api";
import { sipUsersApi } from "@/features/sip-users/api";

export const store = configureStore({
    reducer: {
        [routesApi.reducerPath]: routesApi.reducer,
        [callHistoryApi.reducerPath]: callHistoryApi.reducer,
        [sipUsersApi.reducerPath]: sipUsersApi.reducer,
        [registrationsApi.reducerPath]: registrationsApi.reducer,
        [activeCallsApi.reducerPath]: activeCallsApi.reducer,
    },
    middleware: (getDefaultMiddleware) =>
        getDefaultMiddleware().concat(
            routesApi.middleware,
            callHistoryApi.middleware,
            sipUsersApi.middleware,
            registrationsApi.middleware,
            activeCallsApi.middleware,
        ),
});

export type RootState = ReturnType<typeof store.getState>;
export type AppDispatch = typeof store.dispatch;
