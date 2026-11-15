import { Outlet, useMatches } from "react-router-dom";

import { AppSidebar } from "@/components/layout/AppSidebar";
import { ThemeToggle } from "@/components/layout/ThemeToggle";
import { Separator } from "@/components/ui/separator";
import { SidebarInset, SidebarProvider, SidebarTrigger } from "@/components/ui/sidebar";

export function AppShell() {
    const matches = useMatches();
    const title = [...matches]
        .reverse()
        .map((match) => (match.handle as { title?: string } | undefined)?.title)
        .find((value): value is string => typeof value === "string");

    return (
        <SidebarProvider>
            <AppSidebar />
            <SidebarInset>
                <header className="sticky top-0 z-10 flex h-14 shrink-0 items-center justify-between border-b bg-background/95 px-4 backdrop-blur supports-[backdrop-filter]:bg-background/60">
                    <div className="flex items-center gap-3">
                        <SidebarTrigger />
                        <Separator orientation="vertical" className="h-4" />
                        <span className="text-sm font-medium text-muted-foreground">{title ?? "Control Plane"}</span>
                    </div>
                    <ThemeToggle />
                </header>
                <main className="flex-1 overflow-auto p-6">
                    <Outlet />
                </main>
            </SidebarInset>
        </SidebarProvider>
    );
}
