import { zodResolver } from "@hookform/resolvers/zod";
import { useEffect, useRef, useState } from "react";
import { useForm } from "react-hook-form";
import { toast } from "sonner";
import { z } from "zod";

import { Button } from "@/components/ui/button";
import {
    Dialog,
    DialogContent,
    DialogDescription,
    DialogFooter,
    DialogHeader,
    DialogTitle,
} from "@/components/ui/dialog";
import { Form, FormControl, FormField, FormItem, FormLabel, FormMessage } from "@/components/ui/form";
import { Input } from "@/components/ui/input";
import { useCreateRouteMutation, useGetRoutesQuery, useUpdateRouteMutation } from "@/features/routes/api";
import { SwapPriorityAlert } from "@/features/routes/components/SwapPriorityAlert";
import type { RouteRuleWithKey, SwapRoutePayload } from "@/features/routes/types";
import { getApiErrorMessage } from "@/lib/api";

const routeFormSchema = z.object({
    priority: z.coerce.number().int().min(1, "Priority must be a positive integer"),
    uri: z.string().min(1, "URI is required"),
    sip_address: z.string().min(1, "SIP address is required"),
    port: z.coerce.number().int().min(1, "Port must be between 1-65535").max(65535, "Port must be between 1-65535"),
    codec: z.string().optional(),
});

type RouteFormValues = z.infer<typeof routeFormSchema>;

const emptyValues: RouteFormValues = { priority: 1, uri: "", sip_address: "", port: 5060, codec: "" };

interface RouteFormDialogProps {
    open: boolean;
    onOpenChange: (open: boolean) => void;
    route?: RouteRuleWithKey;
}

export function RouteFormDialog({ open, onOpenChange, route }: RouteFormDialogProps) {
    const isEdit = Boolean(route);
    const { data } = useGetRoutesQuery();
    const [createRoute, { isLoading: isCreating }] = useCreateRouteMutation();
    const [updateRoute, { isLoading: isUpdating }] = useUpdateRouteMutation();
    const isSubmitting = isCreating || isUpdating;

    const [pendingSwap, setPendingSwap] = useState<SwapRoutePayload | undefined>(undefined);
    const [collidingRoute, setCollidingRoute] = useState<RouteRuleWithKey | undefined>(undefined);

    const form = useForm<RouteFormValues>({
        resolver: zodResolver(routeFormSchema),
        defaultValues: emptyValues,
    });

    // Read via a ref rather than a `data` dependency below, so a background
    // refetch while the create dialog is open doesn't stomp on what the user
    // already typed.
    const dataRef = useRef(data);
    dataRef.current = data;

    useEffect(() => {
        if (!open) return;
        if (route) {
            form.reset({ ...route, codec: route.codec ?? "" });
        } else {
            const priorities = dataRef.current ? Object.keys(dataRef.current.routes).map(Number) : [];
            const nextPriority = priorities.length > 0 ? Math.max(...priorities) + 1 : 1;
            form.reset({ ...emptyValues, priority: nextPriority });
        }
        setPendingSwap(undefined);
        setCollidingRoute(undefined);
    }, [open, route, form]);

    const onSubmit = async (values: RouteFormValues) => {
        const payload = { ...values, codec: values.codec ? values.codec : null };

        if (isEdit && route && payload.priority !== route.priority) {
            const collision = data?.routes[String(payload.priority)];
            if (collision) {
                setCollidingRoute({ priority: payload.priority, ...collision });
                setPendingSwap({ currentPriority: route.priority, targetPriority: payload.priority, ...payload });
                return;
            }
        }

        try {
            if (isEdit && route) {
                await updateRoute({ ...payload, currentPriority: route.priority }).unwrap();
                toast.success(`Route with priority ${payload.priority} updated`);
            } else {
                await createRoute(payload).unwrap();
                toast.success(`Route with priority ${payload.priority} created`);
            }
            onOpenChange(false);
        } catch (error) {
            toast.error(getApiErrorMessage(error));
        }
    };

    const closeSwapAlert = (open: boolean) => {
        if (!open) {
            setPendingSwap(undefined);
            setCollidingRoute(undefined);
        }
    };

    const handleSwapped = () => {
        setPendingSwap(undefined);
        setCollidingRoute(undefined);
        onOpenChange(false);
    };

    return (
        <Dialog open={open} onOpenChange={onOpenChange}>
            <DialogContent
                onOpenAutoFocus={(event) => {
                    event.preventDefault();
                    form.setFocus("uri");
                }}
            >
                <DialogHeader>
                    <DialogTitle>{isEdit ? "Edit route" : "Add route"}</DialogTitle>
                    <DialogDescription>
                        {isEdit
                            ? "Update the destination and priority for this route."
                            : "Define a new route's priority and SIP destination."}
                    </DialogDescription>
                </DialogHeader>
                <Form {...form}>
                    <form onSubmit={form.handleSubmit(onSubmit)} className="space-y-4">
                        <FormField
                            control={form.control}
                            name="priority"
                            render={({ field }) => (
                                <FormItem>
                                    <FormLabel>Priority</FormLabel>
                                    <FormControl>
                                        <Input {...field} type="number" min={1} placeholder="1" className="font-mono" />
                                    </FormControl>
                                    <FormMessage />
                                </FormItem>
                            )}
                        />
                        <FormField
                            control={form.control}
                            name="uri"
                            render={({ field }) => (
                                <FormItem>
                                    <FormLabel>URI</FormLabel>
                                    <FormControl>
                                        <Input {...field} placeholder="sip:callee@127.0.0.1" className="font-mono" />
                                    </FormControl>
                                    <FormMessage />
                                </FormItem>
                            )}
                        />
                        <FormField
                            control={form.control}
                            name="sip_address"
                            render={({ field }) => (
                                <FormItem>
                                    <FormLabel>SIP address</FormLabel>
                                    <FormControl>
                                        <Input {...field} placeholder="127.0.0.1" className="font-mono" />
                                    </FormControl>
                                    <FormMessage />
                                </FormItem>
                            )}
                        />
                        <div className="grid grid-cols-2 gap-4">
                            <FormField
                                control={form.control}
                                name="port"
                                render={({ field }) => (
                                    <FormItem>
                                        <FormLabel>Port</FormLabel>
                                        <FormControl>
                                            <Input {...field} type="number" min={1} max={65535} className="font-mono" />
                                        </FormControl>
                                        <FormMessage />
                                    </FormItem>
                                )}
                            />
                            <FormField
                                control={form.control}
                                name="codec"
                                render={({ field }) => (
                                    <FormItem>
                                        <FormLabel>Codec (optional)</FormLabel>
                                        <FormControl>
                                            <Input {...field} placeholder="PCMU" className="font-mono" />
                                        </FormControl>
                                        <FormMessage />
                                    </FormItem>
                                )}
                            />
                        </div>
                        <DialogFooter>
                            <Button type="button" variant="outline" onClick={() => onOpenChange(false)}>
                                Cancel
                            </Button>
                            <Button type="submit" disabled={isSubmitting}>
                                {isEdit ? "Save changes" : "Create route"}
                            </Button>
                        </DialogFooter>
                    </form>
                </Form>
            </DialogContent>
            <SwapPriorityAlert
                pendingSwap={pendingSwap}
                collidingRoute={collidingRoute}
                onOpenChange={closeSwapAlert}
                onSwapped={handleSwapped}
            />
        </Dialog>
    );
}
