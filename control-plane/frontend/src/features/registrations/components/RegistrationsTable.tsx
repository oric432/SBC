import { formatDistanceToNow } from "date-fns";
import { Radio } from "lucide-react";
import { memo } from "react";

import { Badge } from "@/components/ui/badge";
import {
    Table,
    TableBody,
    TableCell,
    TableHead,
    TableHeader,
    TableRow,
} from "@/components/ui/table";
import {
    Tooltip,
    TooltipContent,
    TooltipProvider,
    TooltipTrigger,
} from "@/components/ui/tooltip";
import type { Registration } from "@/features/registrations/types";

interface RegistrationsTableProps {
    registrations: Registration[];
}

// expiresAt is written by the engine at grant time and never updated in
// between refreshes, so a row can be past it while still live (the phone
// just hasn't refreshed since) -- badge it as "stale" rather than claiming
// certainty either way. See sip_registrations' own comment: this mirror is
// best-effort, not authoritative (BindingStore, in the engine, is).
function ExpiryCell({ expiresAt }: { expiresAt: string }) {
    const date = new Date(expiresAt);
    const isPast = date.getTime() < Date.now();
    return (
        <Tooltip>
            <TooltipTrigger asChild>
                <span className={isPast ? "text-muted-foreground" : undefined}>
                    {isPast ? "stale" : `in ${formatDistanceToNow(date)}`}
                </span>
            </TooltipTrigger>
            <TooltipContent>{date.toLocaleString()}</TooltipContent>
        </Tooltip>
    );
}

export const RegistrationsTable = memo(function RegistrationsTable({
    registrations,
}: RegistrationsTableProps) {
    if (registrations.length === 0) {
        return (
            <div className="flex h-40 flex-col items-center justify-center gap-2 rounded-md border border-dashed text-muted-foreground">
                <Radio className="h-6 w-6" />
                <p className="text-sm">No active registrations.</p>
            </div>
        );
    }

    return (
        <TooltipProvider delayDuration={300}>
            <div className="overflow-hidden rounded-md border">
                <Table>
                    <TableHeader>
                        <TableRow className="hover:bg-transparent">
                            <TableHead>AOR</TableHead>
                            <TableHead>Contact</TableHead>
                            <TableHead>Source</TableHead>
                            <TableHead>Transport</TableHead>
                            <TableHead>Expires</TableHead>
                            <TableHead>User-Agent</TableHead>
                        </TableRow>
                    </TableHeader>
                    <TableBody>
                        {registrations.map((registration) => (
                            <TableRow key={registration.id}>
                                <TableCell className="font-mono text-sm font-medium">
                                    {registration.aor}
                                </TableCell>
                                <TableCell className="max-w-[200px] font-mono text-sm">
                                    <Tooltip>
                                        <TooltipTrigger asChild>
                                            <span className="block truncate">
                                                {registration.contactUri}
                                            </span>
                                        </TooltipTrigger>
                                        <TooltipContent className="font-mono">
                                            {registration.contactUri}
                                        </TooltipContent>
                                    </Tooltip>
                                </TableCell>
                                <TableCell className="font-mono text-sm">
                                    {registration.sourceAddress}:
                                    {registration.sourcePort}
                                </TableCell>
                                <TableCell>
                                    <Badge
                                        variant="secondary"
                                        className="uppercase"
                                    >
                                        {registration.transport}
                                    </Badge>
                                </TableCell>
                                <TableCell className="text-sm">
                                    <ExpiryCell
                                        expiresAt={registration.expiresAt}
                                    />
                                </TableCell>
                                <TableCell className="max-w-[180px] truncate text-sm text-muted-foreground">
                                    {registration.userAgent ?? "—"}
                                </TableCell>
                            </TableRow>
                        ))}
                    </TableBody>
                </Table>
            </div>
        </TooltipProvider>
    );
});
