import { Plus } from "lucide-react";
import { useState } from "react";

import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { useGetRoutesQuery } from "@/features/routes/api";
import { DeleteRouteAlert } from "@/features/routes/components/DeleteRouteAlert";
import { RouteFormDialog } from "@/features/routes/components/RouteFormDialog";
import { RouteTable } from "@/features/routes/components/RouteTable";
import type { RouteRuleWithKey } from "@/features/routes/types";

export function RoutesPage() {
    const { data, isLoading, isError } = useGetRoutesQuery();

    const [formOpen, setFormOpen] = useState(false);
    const [editingRoute, setEditingRoute] = useState<RouteRuleWithKey | undefined>(undefined);
    const [deletingRoute, setDeletingRoute] = useState<RouteRuleWithKey | undefined>(undefined);

    const routes: RouteRuleWithKey[] = data
        ? Object.entries(data.routes).map(([route_key, rule]) => ({ route_key, ...rule }))
        : [];

    const openCreateDialog = () => {
        setEditingRoute(undefined);
        setFormOpen(true);
    };

    const openEditDialog = (route: RouteRuleWithKey) => {
        setEditingRoute(route);
        setFormOpen(true);
    };

    return (
        <div className="space-y-4">
            <div className="flex items-center justify-between">
                <div>
                    <h1 className="text-2xl font-semibold tracking-tight">Routes</h1>
                    <p className="text-sm text-muted-foreground">B2BUA route table used to send INVITEs to their destination.</p>
                </div>
                <Button onClick={openCreateDialog}>
                    <Plus className="mr-2 h-4 w-4" />
                    Add route
                </Button>
            </div>

            {isLoading ? (
                <div className="space-y-2">
                    <Skeleton className="h-10 w-full" />
                    <Skeleton className="h-10 w-full" />
                    <Skeleton className="h-10 w-full" />
                </div>
            ) : isError ? (
                <div className="flex h-32 items-center justify-center rounded-md border border-dashed text-sm text-destructive">
                    Failed to load routes.
                </div>
            ) : (
                <RouteTable routes={routes} onEdit={openEditDialog} onDelete={setDeletingRoute} />
            )}

            <RouteFormDialog open={formOpen} onOpenChange={setFormOpen} route={editingRoute} />
            <DeleteRouteAlert
                open={Boolean(deletingRoute)}
                onOpenChange={(open) => !open && setDeletingRoute(undefined)}
                routeKey={deletingRoute?.route_key ?? ""}
            />
        </div>
    );
}
