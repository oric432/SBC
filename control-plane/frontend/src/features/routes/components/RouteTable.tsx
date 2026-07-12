import { MoreHorizontal, Pencil, Trash2 } from "lucide-react";

import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
    DropdownMenu,
    DropdownMenuContent,
    DropdownMenuItem,
    DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import type { RouteRuleWithKey } from "@/features/routes/types";

interface RouteTableProps {
    routes: RouteRuleWithKey[];
    onEdit: (route: RouteRuleWithKey) => void;
    onDelete: (route: RouteRuleWithKey) => void;
}

export function RouteTable({ routes, onEdit, onDelete }: RouteTableProps) {
    if (routes.length === 0) {
        return (
            <div className="flex h-32 items-center justify-center rounded-md border border-dashed text-sm text-muted-foreground">
                No routes yet. Add one to get started.
            </div>
        );
    }

    return (
        <div className="rounded-md border">
            <Table>
                <TableHeader>
                    <TableRow>
                        <TableHead>Route key</TableHead>
                        <TableHead>URI</TableHead>
                        <TableHead>SIP address</TableHead>
                        <TableHead>Port</TableHead>
                        <TableHead>Codec</TableHead>
                        <TableHead className="w-10" />
                    </TableRow>
                </TableHeader>
                <TableBody>
                    {routes.map((route) => (
                        <TableRow key={route.route_key}>
                            <TableCell className="font-medium">{route.route_key}</TableCell>
                            <TableCell>{route.uri}</TableCell>
                            <TableCell>{route.sip_address}</TableCell>
                            <TableCell>{route.port}</TableCell>
                            <TableCell>{route.codec ? <Badge variant="secondary">{route.codec}</Badge> : "—"}</TableCell>
                            <TableCell>
                                <DropdownMenu>
                                    <DropdownMenuTrigger asChild>
                                        <Button variant="ghost" size="icon" className="h-8 w-8">
                                            <MoreHorizontal className="h-4 w-4" />
                                            <span className="sr-only">Open menu</span>
                                        </Button>
                                    </DropdownMenuTrigger>
                                    <DropdownMenuContent align="end">
                                        <DropdownMenuItem onClick={() => onEdit(route)}>
                                            <Pencil className="mr-2 h-4 w-4" />
                                            Edit
                                        </DropdownMenuItem>
                                        <DropdownMenuItem
                                            className="text-destructive focus:text-destructive"
                                            onClick={() => onDelete(route)}
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
}
