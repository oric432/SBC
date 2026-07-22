export interface RouteRule {
    uri: string;
    sip_address: string;
    port: number;
    codec?: string | null;
}

export interface RouteSnapshot {
    table_id: string;
    version: number;
    routes: Record<string, RouteRule>;
}

export interface RouteRuleWithKey extends RouteRule {
    route_key: string;
}

export type CreateRoutePayload = RouteRuleWithKey;

export interface UpdateRoutePayload extends RouteRule {
    route_key: string;
}
