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

interface DeleteRouteAlertProps {
    open: boolean;
    onOpenChange: (open: boolean) => void;
    routeKey: string;
}

export function DeleteRouteAlert({ open, onOpenChange, routeKey }: DeleteRouteAlertProps) {
    const [deleteRoute, { isLoading }] = useDeleteRouteMutation();

    const handleDelete = async () => {
        try {
            await deleteRoute(routeKey).unwrap();
            toast.success(`Route "${routeKey}" deleted`);
            onOpenChange(false);
        } catch {
            toast.error("Failed to delete route");
        }
    };

    return (
        <AlertDialog open={open} onOpenChange={onOpenChange}>
            <AlertDialogContent>
                <AlertDialogHeader>
                    <AlertDialogTitle>Delete route "{routeKey}"?</AlertDialogTitle>
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
