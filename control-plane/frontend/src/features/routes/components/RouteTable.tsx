import { ArrowDown, ArrowUp, MoreHorizontal, Pencil, Trash2 } from "lucide-react";
import { useMemo, useState } from "react";

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

type SortDirection = "asc" | "desc";

interface RouteTableProps {
    routes: RouteRuleWithKey[];
    onEdit: (route: RouteRuleWithKey) => void;
    onDelete: (route: RouteRuleWithKey) => void;
}

export function RouteTable({ routes, onEdit, onDelete }: RouteTableProps) {
    const [sortDirection, setSortDirection] = useState<SortDirection>("asc");

    const sortedRoutes = useMemo(
        () =>
            [...routes].sort((a, b) =>
                sortDirection === "asc" ? a.priority - b.priority : b.priority - a.priority,
            ),
        [routes, sortDirection],
    );

    const toggleSort = () => setSortDirection((prev) => (prev === "asc" ? "desc" : "asc"));

    if (routes.length === 0) {
        return (
            <div className="flex h-32 items-center justify-center rounded-md border border-dashed text-sm text-muted-foreground">
                No routes yet. Add one to get started.
            </div>
        );
    }

    const SortIcon = sortDirection === "asc" ? ArrowUp : ArrowDown;

    return (
        <div className="rounded-md border">
            <Table>
                <TableHeader>
                    <TableRow>
                        <TableHead>
                            <Button
                                variant="ghost"
                                size="sm"
                                className="-ml-3 h-8 px-3"
                                onClick={toggleSort}
                                aria-label={`Sort by priority ${sortDirection === "asc" ? "descending" : "ascending"}`}
                            >
                                Priority
                                <SortIcon className="ml-2 h-4 w-4" />
                            </Button>
                        </TableHead>
                        <TableHead>URI</TableHead>
                        <TableHead>SIP address</TableHead>
                        <TableHead>Port</TableHead>
                        <TableHead>Codec</TableHead>
                        <TableHead className="w-10" />
                    </TableRow>
                </TableHeader>
                <TableBody>
                    {sortedRoutes.map((route) => (
                        <TableRow key={route.priority}>
                            <TableCell className="font-medium">{route.priority}</TableCell>
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
