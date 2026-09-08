// Mirrors schemas/b2bua/sip_route_rule.json's codec enum (generated from the
// engine's Protocols::kSupportedCodecs, src/protocols/SupportedCodecs.hpp) —
// no codegen step, update this by hand if that schema changes.
export const SUPPORTED_CODECS = ["G722", "PCMU", "PCMA"] as const;
export type SupportedCodec = (typeof SUPPORTED_CODECS)[number];

export interface RouteRule {
    uri: string;
    sip_address: string;
    port: number;
    codec?: SupportedCodec | null;
}

export interface RouteSnapshot {
    table_id: string;
    version: number;
    routes: Record<string, RouteRule>;
}

export interface RouteRuleWithKey extends RouteRule {
    priority: number;
}

export type CreateRoutePayload = RouteRuleWithKey;

export interface UpdateRoutePayload extends RouteRule {
    currentPriority: number;
    priority: number;
}

export interface SwapRoutePayload extends RouteRule {
    currentPriority: number;
    targetPriority: number;
}
