import { AlertTriangle } from "lucide-react";
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
import { useDeleteSipUserMutation } from "@/features/sip-users/api";
import type { SipUser } from "@/features/sip-users/types";
import { getApiErrorMessage } from "@/lib/api";

interface DeleteSipUserAlertProps {
    open: boolean;
    onOpenChange: (open: boolean) => void;
    user?: SipUser;
}

export function DeleteSipUserAlert({
    open,
    onOpenChange,
    user,
}: DeleteSipUserAlertProps) {
    const [deleteSipUser, { isLoading }] = useDeleteSipUserMutation();

    const handleDelete = async () => {
        if (!user) return;
        try {
            await deleteSipUser(user.id).unwrap();
            toast.success(`${user.username}@${user.realm} deleted`);
            onOpenChange(false);
        } catch (error) {
            toast.error(getApiErrorMessage(error));
        }
    };

    return (
        <AlertDialog open={open} onOpenChange={onOpenChange}>
            <AlertDialogContent>
                <AlertDialogHeader>
                    <div className="flex h-10 w-10 items-center justify-center rounded-full bg-destructive/10 text-destructive">
                        <AlertTriangle className="h-5 w-5" />
                    </div>
                    <AlertDialogTitle>
                        Delete{" "}
                        <span className="font-mono">
                            {user
                                ? `${user.username}@${user.realm}`
                                : "this user"}
                        </span>
                        ?
                    </AlertDialogTitle>
                    <AlertDialogDescription>
                        Any phone registered as this user will be rejected on
                        its next REGISTER refresh. This action cannot be undone.
                    </AlertDialogDescription>
                </AlertDialogHeader>
                <AlertDialogFooter>
                    <AlertDialogCancel>Cancel</AlertDialogCancel>
                    <AlertDialogAction
                        onClick={handleDelete}
                        disabled={isLoading}
                    >
                        Delete
                    </AlertDialogAction>
                </AlertDialogFooter>
            </AlertDialogContent>
        </AlertDialog>
    );
}
