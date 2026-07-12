import { createBrowserRouter, Navigate } from "react-router-dom";

import { AppShell } from "@/components/layout/AppShell";
import { RoutesPage } from "@/features/routes/RoutesPage";

export const router = createBrowserRouter([
    {
        path: "/",
        element: <AppShell />,
        children: [
            { index: true, element: <Navigate to="/routes" replace /> },
            { path: "routes", element: <RoutesPage /> },
        ],
    },
]);
