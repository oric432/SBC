import { Filter } from "lucide-react";

import { Button } from "@/components/ui/button";
import {
    DropdownMenu,
    DropdownMenuCheckboxItem,
    DropdownMenuContent,
    DropdownMenuLabel,
    DropdownMenuSeparator,
    DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { CALL_STATUSES, type CallStatus } from "@/features/call-history/types";

interface CallHistoryStatusFilterProps {
    selected: CallStatus[];
    onChange: (statuses: CallStatus[]) => void;
}

export function CallHistoryStatusFilter({ selected, onChange }: CallHistoryStatusFilterProps) {
    const toggleStatus = (status: CallStatus, checked: boolean) => {
        onChange(checked ? [...selected, status] : selected.filter((existing) => existing !== status));
    };

    return (
        <DropdownMenu>
            <DropdownMenuTrigger asChild>
                <Button variant="outline" size="sm">
                    <Filter className="mr-2 h-4 w-4" />
                    Status{selected.length > 0 ? ` (${selected.length})` : ""}
                </Button>
            </DropdownMenuTrigger>
            <DropdownMenuContent align="start" className="w-48">
                <DropdownMenuLabel>Filter by status</DropdownMenuLabel>
                <DropdownMenuSeparator />
                {CALL_STATUSES.map((status) => (
                    <DropdownMenuCheckboxItem
                        key={status}
                        checked={selected.includes(status)}
                        onCheckedChange={(checked) => toggleStatus(status, checked)}
                        onSelect={(event) => event.preventDefault()}
                    >
                        {status}
                    </DropdownMenuCheckboxItem>
                ))}
            </DropdownMenuContent>
        </DropdownMenu>
    );
}
