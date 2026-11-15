import { AlertTriangle, Plus, Waypoints } from "lucide-react";
import { useCallback, useMemo, useState } from "react";

import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { useGetRoutesQuery } from "@/features/routes/api";
import { DeleteRouteAlert } from "@/features/routes/components/DeleteRouteAlert";
import { RouteFormDialog } from "@/features/routes/components/RouteFormDialog";
import { RouteTable } from "@/features/routes/components/RouteTable";
import type { RouteRuleWithKey } from "@/features/routes/types";
import { getApiErrorMessage } from "@/lib/api";

export function RoutesPage() {
    const { data, isLoading, isError, error } = useGetRoutesQuery();

    const [formOpen, setFormOpen] = useState(false);
    const [editingRoute, setEditingRoute] = useState<RouteRuleWithKey | undefined>(undefined);
    const [deletingRoute, setDeletingRoute] = useState<RouteRuleWithKey | undefined>(undefined);

    // Keeps a stable array reference across re-renders with the same data,
    // so RouteTable's own sort memoization isn't invalidated needlessly.
    const routes = useMemo<RouteRuleWithKey[]>(
        () => (data ? Object.entries(data.routes).map(([priority, rule]) => ({ priority: Number(priority), ...rule })) : []),
        [data],
    );

    const openCreateDialog = useCallback(() => {
        setEditingRoute(undefined);
        setFormOpen(true);
    }, []);

    const openEditDialog = useCallback((route: RouteRuleWithKey) => {
        setEditingRoute(route);
        setFormOpen(true);
    }, []);

    const closeDeleteAlert = useCallback((open: boolean) => {
        if (!open) setDeletingRoute(undefined);
    }, []);

    return (
        <div className="space-y-6">
            <div className="flex items-start justify-between gap-4">
                <div className="flex items-center gap-3">
                    <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-lg bg-primary/10 text-primary">
                        <Waypoints className="h-5 w-5" />
                    </div>
                    <div>
                        <h1 className="text-2xl font-semibold tracking-tight">Routes</h1>
                        <p className="text-sm text-muted-foreground">B2BUA route table used to send INVITEs to their destination.</p>
                    </div>
                </div>
                <Button onClick={openCreateDialog}>
                    <Plus className="mr-2 h-4 w-4" />
                    Add route
                </Button>
            </div>

            {!isLoading && !isError && (
                <div className="flex items-center gap-2 text-sm text-muted-foreground">
                    <span className="font-mono tabular-nums text-foreground">{routes.length}</span>
                    {routes.length === 1 ? "route configured" : "routes configured"}
                </div>
            )}

            {isLoading ? (
                <div className="space-y-2 rounded-md border p-2">
                    <Skeleton className="h-8 w-full" />
                    <Skeleton className="h-10 w-full" />
                    <Skeleton className="h-10 w-full" />
                    <Skeleton className="h-10 w-full" />
                </div>
            ) : isError ? (
                <div className="flex h-32 flex-col items-center justify-center gap-2 rounded-md border border-dashed text-sm text-destructive">
                    <AlertTriangle className="h-5 w-5" />
                    {getApiErrorMessage(error)}
                </div>
            ) : (
                <RouteTable routes={routes} onEdit={openEditDialog} onDelete={setDeletingRoute} />
            )}

            <RouteFormDialog open={formOpen} onOpenChange={setFormOpen} route={editingRoute} />
            <DeleteRouteAlert open={Boolean(deletingRoute)} onOpenChange={closeDeleteAlert} priority={deletingRoute?.priority ?? 0} />
        </div>
    );
}
