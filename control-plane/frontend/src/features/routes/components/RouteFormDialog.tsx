import { zodResolver } from "@hookform/resolvers/zod";
import { useEffect } from "react";
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
import { useCreateRouteMutation, useUpdateRouteMutation } from "@/features/routes/api";
import type { RouteRuleWithKey } from "@/features/routes/types";
import { getApiErrorMessage } from "@/lib/api";

const routeFormSchema = z.object({
    route_key: z.string().min(1, "Route key is required"),
    uri: z.string().min(1, "URI is required"),
    sip_address: z.string().min(1, "SIP address is required"),
    port: z.coerce.number().int().min(1, "Port must be between 1-65535").max(65535, "Port must be between 1-65535"),
    codec: z.string().optional(),
});

type RouteFormValues = z.infer<typeof routeFormSchema>;

const emptyValues: RouteFormValues = { route_key: "", uri: "", sip_address: "", port: 5060, codec: "" };

interface RouteFormDialogProps {
    open: boolean;
    onOpenChange: (open: boolean) => void;
    route?: RouteRuleWithKey;
}

export function RouteFormDialog({ open, onOpenChange, route }: RouteFormDialogProps) {
    const isEdit = Boolean(route);
    const [createRoute, { isLoading: isCreating }] = useCreateRouteMutation();
    const [updateRoute, { isLoading: isUpdating }] = useUpdateRouteMutation();
    const isSubmitting = isCreating || isUpdating;

    const form = useForm<RouteFormValues>({
        resolver: zodResolver(routeFormSchema),
        defaultValues: emptyValues,
    });

    useEffect(() => {
        if (!open) return;
        form.reset(route ? { ...route, codec: route.codec ?? "" } : emptyValues);
    }, [open, route, form]);

    const onSubmit = async (values: RouteFormValues) => {
        const payload = { ...values, codec: values.codec ? values.codec : null };
        try {
            if (isEdit) {
                await updateRoute(payload).unwrap();
                toast.success(`Route "${payload.route_key}" updated`);
            } else {
                await createRoute(payload).unwrap();
                toast.success(`Route "${payload.route_key}" created`);
            }
            onOpenChange(false);
        } catch (error) {
            toast.error(getApiErrorMessage(error));
        }
    };

    return (
        <Dialog open={open} onOpenChange={onOpenChange}>
            <DialogContent>
                <DialogHeader>
                    <DialogTitle>{isEdit ? "Edit route" : "Add route"}</DialogTitle>
                    <DialogDescription>
                        {isEdit
                            ? "Update the destination for this route key."
                            : "Define a new route key and its SIP destination."}
                    </DialogDescription>
                </DialogHeader>
                <Form {...form}>
                    <form onSubmit={form.handleSubmit(onSubmit)} className="space-y-4">
                        <FormField
                            control={form.control}
                            name="route_key"
                            render={({ field }) => (
                                <FormItem>
                                    <FormLabel>Route key</FormLabel>
                                    <FormControl>
                                        <Input {...field} disabled={isEdit} placeholder="callee" />
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
                                        <Input {...field} placeholder="sip:callee@127.0.0.1:5080" />
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
                                        <Input {...field} placeholder="127.0.0.1" />
                                    </FormControl>
                                    <FormMessage />
                                </FormItem>
                            )}
                        />
                        <FormField
                            control={form.control}
                            name="port"
                            render={({ field }) => (
                                <FormItem>
                                    <FormLabel>Port</FormLabel>
                                    <FormControl>
                                        <Input {...field} type="number" min={1} max={65535} />
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
                                        <Input {...field} placeholder="PCMU" />
                                    </FormControl>
                                    <FormMessage />
                                </FormItem>
                            )}
                        />
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
        </Dialog>
    );
}
