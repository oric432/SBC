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
                    <AlertDialogTitle>Delete route with priority {priority}?</AlertDialogTitle>
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
