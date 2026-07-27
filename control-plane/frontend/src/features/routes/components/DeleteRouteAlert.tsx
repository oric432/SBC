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
import { useDeleteRouteMutation } from "@/features/routes/api";
import { getApiErrorMessage } from "@/lib/api";

interface DeleteRouteAlertProps {
    open: boolean;
    onOpenChange: (open: boolean) => void;
    priority: number;
}

export function DeleteRouteAlert({ open, onOpenChange, priority }: DeleteRouteAlertProps) {
    const [deleteRoute, { isLoading }] = useDeleteRouteMutation();

    const handleDelete = async () => {
        try {
            await deleteRoute(priority).unwrap();
            toast.success(`Route with priority ${priority} deleted`);
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
                        Delete route with priority <span className="font-mono">{priority}</span>?
                    </AlertDialogTitle>
                    <AlertDialogDescription>
                        This will permanently remove this route from the table. This action cannot be undone.
                    </AlertDialogDescription>
                </AlertDialogHeader>
                <AlertDialogFooter>
                    <AlertDialogCancel>Cancel</AlertDialogCancel>
                    <AlertDialogAction onClick={handleDelete} disabled={isLoading}>
                        Delete
                    </AlertDialogAction>
                </AlertDialogFooter>
            </AlertDialogContent>
        </AlertDialog>
    );
}
