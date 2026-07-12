import { RouterProvider } from "react-router-dom";

import { ThemeProvider } from "@/components/layout/ThemeProvider";
import { Toaster } from "@/components/ui/sonner";
import { router } from "@/routes/router";

export function App() {
    return (
        <ThemeProvider attribute="class" defaultTheme="system" enableSystem>
            <RouterProvider router={router} />
            <Toaster />
        </ThemeProvider>
    );
}
