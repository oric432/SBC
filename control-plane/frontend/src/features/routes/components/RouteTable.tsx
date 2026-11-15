import { ArrowDown, ArrowUp, MoreHorizontal, Pencil, Route, Trash2 } from "lucide-react";
import { memo, useMemo, useState } from "react";

import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
    DropdownMenu,
    DropdownMenuContent,
    DropdownMenuItem,
    DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Tooltip, TooltipContent, TooltipProvider, TooltipTrigger } from "@/components/ui/tooltip";
import type { RouteRuleWithKey } from "@/features/routes/types";

type SortDirection = "asc" | "desc";

interface RouteTableProps {
    routes: RouteRuleWithKey[];
    onEdit: (route: RouteRuleWithKey) => void;
    onDelete: (route: RouteRuleWithKey) => void;
}

export const RouteTable = memo(function RouteTable({ routes, onEdit, onDelete }: RouteTableProps) {
    const [sortDirection, setSortDirection] = useState<SortDirection>("asc");

    const sortedRoutes = useMemo(
        () => routes.toSorted((a, b) => (sortDirection === "asc" ? a.priority - b.priority : b.priority - a.priority)),
        [routes, sortDirection],
    );

    const toggleSort = () => setSortDirection((prev) => (prev === "asc" ? "desc" : "asc"));

    if (routes.length === 0) {
        return (
            <div className="flex h-40 flex-col items-center justify-center gap-2 rounded-md border border-dashed text-muted-foreground">
                <Route className="h-6 w-6" />
                <p className="text-sm">No routes yet. Add one to get started.</p>
            </div>
        );
    }

    const SortIcon = sortDirection === "asc" ? ArrowUp : ArrowDown;

    return (
        <TooltipProvider delayDuration={300}>
            <div className="overflow-hidden rounded-md border">
                <Table>
                    <TableHeader>
                        <TableRow className="hover:bg-transparent">
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
                            <TableRow key={route.priority} className="group">
                                <TableCell className="font-mono tabular-nums font-medium">{route.priority}</TableCell>
                                <TableCell className="max-w-[220px] font-mono text-sm">
                                    <Tooltip>
                                        <TooltipTrigger asChild>
                                            <span className="block truncate">{route.uri}</span>
                                        </TooltipTrigger>
                                        <TooltipContent className="font-mono">{route.uri}</TooltipContent>
                                    </Tooltip>
                                </TableCell>
                                <TableCell className="font-mono text-sm">{route.sip_address}</TableCell>
                                <TableCell className="font-mono tabular-nums text-sm">{route.port}</TableCell>
                                <TableCell>{route.codec ? <Badge variant="secondary">{route.codec}</Badge> : "—"}</TableCell>
                                <TableCell>
                                    <DropdownMenu>
                                        <DropdownMenuTrigger asChild>
                                            <Button
                                                variant="ghost"
                                                size="icon"
                                                className="h-8 w-8 opacity-0 transition-opacity focus-visible:opacity-100 group-hover:opacity-100 data-[state=open]:opacity-100"
                                            >
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
        </TooltipProvider>
    );
});
