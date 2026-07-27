import { History, Radio, Waypoints } from "lucide-react";
import { NavLink } from "react-router-dom";

import {
    Sidebar,
    SidebarContent,
    SidebarFooter,
    SidebarGroup,
    SidebarGroupContent,
    SidebarGroupLabel,
    SidebarHeader,
    SidebarMenu,
    SidebarMenuBadge,
    SidebarMenuButton,
    SidebarMenuItem,
} from "@/components/ui/sidebar";
import { useUnseenCallCount } from "@/features/call-history/unseen";

const navItems = [
    { title: "Routes", url: "/routes", icon: Waypoints },
    { title: "Call History", url: "/call-history", icon: History },
];

export function AppSidebar() {
    const unseenCallCount = useUnseenCallCount();

    return (
        <Sidebar>
            <SidebarHeader className="px-4 py-3.5">
                <div className="flex items-center gap-2.5">
                    <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded-md bg-sidebar-primary text-sidebar-primary-foreground">
                        <Radio className="h-4 w-4" />
                    </div>
                    <div className="flex flex-col leading-none">
                        <span className="text-sm font-semibold tracking-tight">SBC Control Plane</span>
                        <span className="text-xs text-sidebar-foreground/60">Session Border Controller</span>
                    </div>
                </div>
            </SidebarHeader>
            <SidebarContent>
                <SidebarGroup>
                    <SidebarGroupLabel>Manage</SidebarGroupLabel>
                    <SidebarGroupContent>
                        <SidebarMenu>
                            {navItems.map((item) => (
                                <SidebarMenuItem key={item.url}>
                                    <SidebarMenuButton asChild>
                                        <NavLink
                                            to={item.url}
                                            className={({ isActive }) =>
                                                isActive
                                                    ? "bg-sidebar-accent font-medium text-sidebar-accent-foreground"
                                                    : ""
                                            }
                                        >
                                            <item.icon />
                                            <span>{item.title}</span>
                                        </NavLink>
                                    </SidebarMenuButton>
                                    {item.url === "/call-history" && unseenCallCount > 0 && (
                                        <SidebarMenuBadge>{unseenCallCount}</SidebarMenuBadge>
                                    )}
                                </SidebarMenuItem>
                            ))}
                        </SidebarMenu>
                    </SidebarGroupContent>
                </SidebarGroup>
            </SidebarContent>
            <SidebarFooter className="px-4 py-3 text-xs text-sidebar-foreground/50">v0.1.0</SidebarFooter>
        </Sidebar>
    );
}
