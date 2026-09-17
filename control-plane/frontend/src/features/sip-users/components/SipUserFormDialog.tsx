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
import {
    Form,
    FormControl,
    FormDescription,
    FormField,
    FormItem,
    FormLabel,
    FormMessage,
} from "@/components/ui/form";
import { Input } from "@/components/ui/input";
import {
    Select,
    SelectContent,
    SelectItem,
    SelectTrigger,
    SelectValue,
} from "@/components/ui/select";
import {
    useCreateSipUserMutation,
    useUpdateSipUserMutation,
} from "@/features/sip-users/api";
import type { SipUser } from "@/features/sip-users/types";
import { getApiErrorMessage } from "@/lib/api";

const ENABLED = "enabled" as const;
const DISABLED = "disabled" as const;

// password is optional at the schema level (blank means "keep the current
// one" when editing) and enforced as required on create in onSubmit instead
// -- a single static schema is simpler than switching resolvers by mode.
const sipUserFormSchema = z.object({
    username: z.string().min(1, "Username is required"),
    realm: z.string().min(1, "Realm is required"),
    password: z.string().optional(),
    status: z.enum([ENABLED, DISABLED]),
});

type SipUserFormValues = z.infer<typeof sipUserFormSchema>;

const emptyValues: SipUserFormValues = {
    username: "",
    realm: "",
    password: "",
    status: ENABLED,
};

interface SipUserFormDialogProps {
    open: boolean;
    onOpenChange: (open: boolean) => void;
    user?: SipUser;
}

export function SipUserFormDialog({
    open,
    onOpenChange,
    user,
}: SipUserFormDialogProps) {
    const isEdit = Boolean(user);
    const [createSipUser, { isLoading: isCreating }] =
        useCreateSipUserMutation();
    const [updateSipUser, { isLoading: isUpdating }] =
        useUpdateSipUserMutation();
    const isSubmitting = isCreating || isUpdating;

    const form = useForm<SipUserFormValues>({
        resolver: zodResolver(sipUserFormSchema),
        defaultValues: emptyValues,
    });

    useEffect(() => {
        if (!open) return;
        form.reset(
            user
                ? {
                      username: user.username,
                      realm: user.realm,
                      password: "",
                      status: user.enabled ? ENABLED : DISABLED,
                  }
                : emptyValues,
        );
    }, [open, user, form]);

    const onSubmit = async (values: SipUserFormValues) => {
        if (!isEdit && !values.password) {
            form.setError("password", { message: "Password is required" });
            return;
        }

        const enabled = values.status === ENABLED;
        try {
            if (isEdit && user) {
                await updateSipUser({
                    id: user.id,
                    enabled,
                    ...(values.password ? { password: values.password } : {}),
                }).unwrap();
                toast.success(`${user.username}@${user.realm} updated`);
            } else {
                await createSipUser({
                    username: values.username,
                    realm: values.realm,
                    password: values.password ?? "",
                    enabled,
                }).unwrap();
                toast.success(`${values.username}@${values.realm} created`);
            }
            onOpenChange(false);
        } catch (error) {
            toast.error(getApiErrorMessage(error));
        }
    };

    return (
        <Dialog open={open} onOpenChange={onOpenChange}>
            <DialogContent
                onOpenAutoFocus={(event) => {
                    event.preventDefault();
                    form.setFocus(isEdit ? "password" : "username");
                }}
            >
                <DialogHeader>
                    <DialogTitle>
                        {isEdit ? "Edit SIP user" : "Add SIP user"}
                    </DialogTitle>
                    <DialogDescription>
                        {isEdit
                            ? "Username and realm can't be changed after creation — delete and recreate to rename."
                            : "Provisions a SIP account the engine can authenticate REGISTER requests against."}
                    </DialogDescription>
                </DialogHeader>
                <Form {...form}>
                    <form
                        onSubmit={form.handleSubmit(onSubmit)}
                        className="space-y-4"
                    >
                        <FormField
                            control={form.control}
                            name="username"
                            render={({ field }) => (
                                <FormItem>
                                    <FormLabel>Username</FormLabel>
                                    <FormControl>
                                        <Input
                                            {...field}
                                            disabled={isEdit}
                                            placeholder="alice"
                                            className="font-mono"
                                        />
                                    </FormControl>
                                    <FormMessage />
                                </FormItem>
                            )}
                        />
                        <FormField
                            control={form.control}
                            name="realm"
                            render={({ field }) => (
                                <FormItem>
                                    <FormLabel>Realm</FormLabel>
                                    <FormControl>
                                        <Input
                                            {...field}
                                            disabled={isEdit}
                                            placeholder="sbc.local"
                                            className="font-mono"
                                        />
                                    </FormControl>
                                    <FormMessage />
                                </FormItem>
                            )}
                        />
                        <FormField
                            control={form.control}
                            name="password"
                            render={({ field }) => (
                                <FormItem>
                                    <FormLabel>
                                        {isEdit ? "New password" : "Password"}
                                    </FormLabel>
                                    <FormControl>
                                        <Input
                                            {...field}
                                            type="password"
                                            placeholder={
                                                isEdit
                                                    ? "Leave blank to keep current"
                                                    : undefined
                                            }
                                        />
                                    </FormControl>
                                    {isEdit && (
                                        <FormDescription>
                                            Only a digest hash is stored — leave
                                            blank to keep the current password.
                                        </FormDescription>
                                    )}
                                    <FormMessage />
                                </FormItem>
                            )}
                        />
                        <FormField
                            control={form.control}
                            name="status"
                            render={({ field }) => (
                                <FormItem>
                                    <FormLabel>Status</FormLabel>
                                    <Select
                                        onValueChange={field.onChange}
                                        value={field.value}
                                    >
                                        <FormControl>
                                            <SelectTrigger>
                                                <SelectValue />
                                            </SelectTrigger>
                                        </FormControl>
                                        <SelectContent>
                                            <SelectItem value={ENABLED}>
                                                Enabled
                                            </SelectItem>
                                            <SelectItem value={DISABLED}>
                                                Disabled
                                            </SelectItem>
                                        </SelectContent>
                                    </Select>
                                    <FormDescription>
                                        A disabled user is rejected as if it
                                        didn't exist.
                                    </FormDescription>
                                    <FormMessage />
                                </FormItem>
                            )}
                        />
                        <DialogFooter>
                            <Button
                                type="button"
                                variant="outline"
                                onClick={() => onOpenChange(false)}
                            >
                                Cancel
                            </Button>
                            <Button type="submit" disabled={isSubmitting}>
                                {isEdit ? "Save changes" : "Create user"}
                            </Button>
                        </DialogFooter>
                    </form>
                </Form>
            </DialogContent>
        </Dialog>
    );
}
