import { ArrowLeftRight, ArrowRight } from "lucide-react";
import type { MouseEvent, ReactNode } from "react";
import { toast } from "sonner";

import {
    AlertDialog,
    AlertDialogAction,
    AlertDialogCancel,
    AlertDialogContent,
    AlertDialogDescription,
    AlertDialogFooter,
    AlertDialogHeader,
    AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { Badge } from "@/components/ui/badge";
import { useSwapRouteMutation } from "@/features/routes/api";
import type { RouteRuleWithKey, SwapRoutePayload } from "@/features/routes/types";
import { getApiErrorMessage } from "@/lib/api";
import { cn } from "@/lib/utils";

interface SwapPriorityAlertProps {
    pendingSwap?: SwapRoutePayload;
    collidingRoute?: RouteRuleWithKey;
    onOpenChange: (open: boolean) => void;
    onSwapped: () => void;
}

function DetailRow({ label, value, mono = true }: { label: string; value: ReactNode; mono?: boolean }) {
    return (
        <div className="flex items-baseline justify-between gap-3 text-sm">
            <span className="shrink-0 text-muted-foreground">{label}</span>
            <span className={cn("min-w-0 break-all text-right font-medium", mono && "font-mono")}>{value}</span>
        </div>
    );
}

function RouteSwapCard({
    label,
    fromPriority,
    toPriority,
    uri,
    sip_address,
    port,
    codec,
}: {
    label: string;
    fromPriority: number;
    toPriority: number;
    uri: string;
    sip_address: string;
    port: number;
    codec?: string | null;
}) {
    return (
        <div className="flex-1 space-y-3 rounded-lg border bg-muted/30 p-3">
            <div className="flex items-center justify-between gap-2">
                <span className="text-xs font-semibold uppercase tracking-wide text-muted-foreground">{label}</span>
                <div className="flex items-center gap-1.5 font-mono text-sm tabular-nums">
                    <span className="text-muted-foreground line-through decoration-muted-foreground/50">{fromPriority}</span>
                    <ArrowRight className="h-3 w-3 shrink-0 text-muted-foreground" />
                    <span className="font-semibold text-primary">{toPriority}</span>
                </div>
            </div>
            <div className="space-y-1.5">
                <DetailRow label="URI" value={uri} />
                <DetailRow label="SIP address" value={sip_address} />
                <DetailRow label="Port" value={port} />
                <DetailRow
                    label="Codec"
                    mono={false}
                    value={codec ? <Badge variant="secondary" className="font-mono">{codec}</Badge> : "—"}
                />
            </div>
        </div>
    );
}

export function SwapPriorityAlert({ pendingSwap, collidingRoute, onOpenChange, onSwapped }: SwapPriorityAlertProps) {
    const [swapRoute, { isLoading }] = useSwapRouteMutation();

    const handleSwap = async (event: MouseEvent<HTMLButtonElement>) => {
        // AlertDialogAction closes the dialog on click by default (it's a
        // Radix Close under the hood). Prevent that so the dialog stays open
        // for the duration of the mutation and only closes once we know the
        // outcome.
        event.preventDefault();
        if (!pendingSwap) return;
        try {
            await swapRoute(pendingSwap).unwrap();
            toast.success(`Swapped priorities ${pendingSwap.currentPriority} and ${pendingSwap.targetPriority}`);
            onSwapped();
        } catch (error) {
            toast.error(getApiErrorMessage(error));
            onOpenChange(false);
        }
    };

    return (
        <AlertDialog
            open={Boolean(pendingSwap)}
            onOpenChange={(nextOpen) => {
                if (isLoading) return;
                onOpenChange(nextOpen);
            }}
        >
            <AlertDialogContent className="sm:max-w-xl">
                <AlertDialogHeader>
                    <div className="flex h-10 w-10 items-center justify-center rounded-full bg-primary/10 text-primary">
                        <ArrowLeftRight className="h-5 w-5" />
                    </div>
                    <AlertDialogTitle>
                        Priority <span className="font-mono">{pendingSwap?.targetPriority}</span> is already in use
                    </AlertDialogTitle>
                    <AlertDialogDescription>
                        Swapping will trade priorities between these two routes. Everything else stays as shown.
                    </AlertDialogDescription>
                </AlertDialogHeader>

                {pendingSwap && collidingRoute && (
                    <div className="flex flex-col items-stretch gap-2 sm:flex-row sm:items-center">
                        <RouteSwapCard
                            label="This route"
                            fromPriority={pendingSwap.currentPriority}
                            toPriority={pendingSwap.targetPriority}
                            uri={pendingSwap.uri}
                            sip_address={pendingSwap.sip_address}
                            port={pendingSwap.port}
                            codec={pendingSwap.codec}
                        />
                        <div className="flex items-center justify-center py-1 sm:py-0">
                            <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded-full border bg-background text-muted-foreground">
                                <ArrowLeftRight className="h-4 w-4" />
                            </div>
                        </div>
                        <RouteSwapCard
                            label="Existing route"
                            fromPriority={pendingSwap.targetPriority}
                            toPriority={pendingSwap.currentPriority}
                            uri={collidingRoute.uri}
                            sip_address={collidingRoute.sip_address}
                            port={collidingRoute.port}
                            codec={collidingRoute.codec}
                        />
                    </div>
                )}

                <AlertDialogFooter>
                    <AlertDialogCancel disabled={isLoading}>Cancel</AlertDialogCancel>
                    <AlertDialogAction onClick={handleSwap} disabled={isLoading}>
                        Swap priorities
                    </AlertDialogAction>
                </AlertDialogFooter>
            </AlertDialogContent>
        </AlertDialog>
    );
}
