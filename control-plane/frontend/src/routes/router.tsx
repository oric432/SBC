import { createBrowserRouter, Navigate } from "react-router-dom";

import { AppShell } from "@/components/layout/AppShell";
import { CallHistoryPage } from "@/features/call-history/CallHistoryPage";
import { RoutesPage } from "@/features/routes/RoutesPage";

export const router = createBrowserRouter([
    {
        path: "/",
        element: <AppShell />,
        children: [
            { index: true, element: <Navigate to="/routes" replace /> },
            { path: "routes", element: <RoutesPage />, handle: { title: "Routes" } },
            { path: "call-history", element: <CallHistoryPage />, handle: { title: "Call History" } },
        ],
    },
]);
