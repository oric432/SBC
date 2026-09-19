import { AlertTriangle, Radio } from "lucide-react";

import { Skeleton } from "@/components/ui/skeleton";
import {
    REGISTRATIONS_POLL_INTERVAL_MS,
    useGetRegistrationsQuery,
} from "@/features/registrations/api";
import { RegistrationsTable } from "@/features/registrations/components/RegistrationsTable";
import { getApiErrorMessage } from "@/lib/api";

export function RegistrationsPage() {
    const { data, isLoading, isError, error } = useGetRegistrationsQuery(
        undefined,
        {
            pollingInterval: REGISTRATIONS_POLL_INTERVAL_MS,
        },
    );

    const registrations = data ?? [];

    return (
        <div className="space-y-6">
            <div className="flex items-center gap-3">
                <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-lg bg-primary/10 text-primary">
                    <Radio className="h-5 w-5" />
                </div>
                <div>
                    <h1 className="text-2xl font-semibold tracking-tight">
                        Registrations
                    </h1>
                    <p className="text-sm text-muted-foreground">
                        Live SIP bindings reported by the engine. Read-only — a
                        phone de-registers itself.
                    </p>
                </div>
            </div>

            {!isLoading && !isError && (
                <div className="flex items-center gap-2 text-sm text-muted-foreground">
                    <span className="font-mono tabular-nums text-foreground">
                        {registrations.length}
                    </span>
                    {registrations.length === 1
                        ? "registration on file"
                        : "registrations on file"}
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
                <RegistrationsTable registrations={registrations} />
            )}
        </div>
    );
}
