import { MoreHorizontal, Pencil, Trash2, UserRound } from "lucide-react";
import { memo } from "react";

import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
    DropdownMenu,
    DropdownMenuContent,
    DropdownMenuItem,
    DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import {
    Table,
    TableBody,
    TableCell,
    TableHead,
    TableHeader,
    TableRow,
} from "@/components/ui/table";
import type { SipUser } from "@/features/sip-users/types";

interface SipUserTableProps {
    users: SipUser[];
    onEdit: (user: SipUser) => void;
    onDelete: (user: SipUser) => void;
}

export const SipUserTable = memo(function SipUserTable({
    users,
    onEdit,
    onDelete,
}: SipUserTableProps) {
    if (users.length === 0) {
        return (
            <div className="flex h-40 flex-col items-center justify-center gap-2 rounded-md border border-dashed text-muted-foreground">
                <UserRound className="h-6 w-6" />
                <p className="text-sm">
                    No SIP users yet. Add one to get started.
                </p>
            </div>
        );
    }

    return (
        <div className="overflow-hidden rounded-md border">
            <Table>
                <TableHeader>
                    <TableRow className="hover:bg-transparent">
                        <TableHead>Username</TableHead>
                        <TableHead>Realm</TableHead>
                        <TableHead>Status</TableHead>
                        <TableHead className="w-10" />
                    </TableRow>
                </TableHeader>
                <TableBody>
                    {users.map((user) => (
                        <TableRow key={user.id} className="group">
                            <TableCell className="font-mono text-sm font-medium">
                                {user.username}
                            </TableCell>
                            <TableCell className="font-mono text-sm">
                                {user.realm}
                            </TableCell>
                            <TableCell>
                                <Badge
                                    variant={
                                        user.enabled ? "secondary" : "outline"
                                    }
                                >
                                    {user.enabled ? "Enabled" : "Disabled"}
                                </Badge>
                            </TableCell>
                            <TableCell>
                                <DropdownMenu>
                                    <DropdownMenuTrigger asChild>
                                        <Button
                                            variant="ghost"
                                            size="icon"
                                            className="h-8 w-8 opacity-0 transition-opacity focus-visible:opacity-100 group-hover:opacity-100 data-[state=open]:opacity-100"
                                        >
                                            <MoreHorizontal className="h-4 w-4" />
                                            <span className="sr-only">
                                                Open menu
                                            </span>
                                        </Button>
                                    </DropdownMenuTrigger>
                                    <DropdownMenuContent align="end">
                                        <DropdownMenuItem
                                            onClick={() => onEdit(user)}
                                        >
                                            <Pencil className="mr-2 h-4 w-4" />
                                            Edit
                                        </DropdownMenuItem>
                                        <DropdownMenuItem
                                            className="text-destructive focus:text-destructive"
                                            onClick={() => onDelete(user)}
                                        >
                                            <Trash2 className="mr-2 h-4 w-4" />
                                            Delete
                                        </DropdownMenuItem>
                                    </DropdownMenuContent>
                                </DropdownMenu>
                            </TableCell>
                        </TableRow>
                    ))}
                </TableBody>
            </Table>
        </div>
    );
});
