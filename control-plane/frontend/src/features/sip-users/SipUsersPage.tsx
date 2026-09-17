import { AlertTriangle, Plus, Users } from "lucide-react";
import { useCallback, useState } from "react";

import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { useGetSipUsersQuery } from "@/features/sip-users/api";
import { DeleteSipUserAlert } from "@/features/sip-users/components/DeleteSipUserAlert";
import { SipUserFormDialog } from "@/features/sip-users/components/SipUserFormDialog";
import { SipUserTable } from "@/features/sip-users/components/SipUserTable";
import type { SipUser } from "@/features/sip-users/types";
import { getApiErrorMessage } from "@/lib/api";

export function SipUsersPage() {
    const { data, isLoading, isError, error } = useGetSipUsersQuery();

    const [formOpen, setFormOpen] = useState(false);
    const [editingUser, setEditingUser] = useState<SipUser | undefined>(
        undefined,
    );
    const [deletingUser, setDeletingUser] = useState<SipUser | undefined>(
        undefined,
    );

    const users = data ?? [];

    const openCreateDialog = useCallback(() => {
        setEditingUser(undefined);
        setFormOpen(true);
    }, []);

    const openEditDialog = useCallback((user: SipUser) => {
        setEditingUser(user);
        setFormOpen(true);
    }, []);

    const closeDeleteAlert = useCallback((open: boolean) => {
        if (!open) setDeletingUser(undefined);
    }, []);

    return (
        <div className="space-y-6">
            <div className="flex items-start justify-between gap-4">
                <div className="flex items-center gap-3">
                    <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-lg bg-primary/10 text-primary">
                        <Users className="h-5 w-5" />
                    </div>
                    <div>
                        <h1 className="text-2xl font-semibold tracking-tight">
                            SIP users
                        </h1>
                        <p className="text-sm text-muted-foreground">
                            Accounts the engine authenticates REGISTER requests
                            against.
                        </p>
                    </div>
                </div>
                <Button onClick={openCreateDialog}>
                    <Plus className="mr-2 h-4 w-4" />
                    Add user
                </Button>
            </div>

            {!isLoading && !isError && (
                <div className="flex items-center gap-2 text-sm text-muted-foreground">
                    <span className="font-mono tabular-nums text-foreground">
                        {users.length}
                    </span>
                    {users.length === 1
                        ? "user configured"
                        : "users configured"}
                </div>
            )}

            {isLoading ? (
                <div className="space-y-2 rounded-md border p-2">
                    <Skeleton className="h-8 w-full" />
                    <Skeleton className="h-10 w-full" />
                    <Skeleton className="h-10 w-full" />
                </div>
            ) : isError ? (
                <div className="flex h-32 flex-col items-center justify-center gap-2 rounded-md border border-dashed text-sm text-destructive">
                    <AlertTriangle className="h-5 w-5" />
                    {getApiErrorMessage(error)}
                </div>
            ) : (
                <SipUserTable
                    users={users}
                    onEdit={openEditDialog}
                    onDelete={setDeletingUser}
                />
            )}

            <SipUserFormDialog
                open={formOpen}
                onOpenChange={setFormOpen}
                user={editingUser}
            />
            <DeleteSipUserAlert
                open={Boolean(deletingUser)}
                onOpenChange={closeDeleteAlert}
                user={deletingUser}
            />
        </div>
    );
}
