import { configureStore } from "@reduxjs/toolkit";
import { routesApi } from "@/features/routes/api";

export const store = configureStore({
    reducer: {
        [routesApi.reducerPath]: routesApi.reducer,
    },
    middleware: (getDefaultMiddleware) => getDefaultMiddleware().concat(routesApi.middleware),
});

export type RootState = ReturnType<typeof store.getState>;
export type AppDispatch = typeof store.dispatch;
