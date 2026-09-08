// Mirrors engine/protocols/SipRoutes.hpp (SbcEngine::Protocols) and schemas/b2bua/*.json

// Mirrors schemas/b2bua/sip_route_rule.json's codec enum (generated from the
// engine's Protocols::kSupportedCodecs, src/protocols/SupportedCodecs.hpp) —
// no codegen step, update this by hand if that schema changes.
export const SUPPORTED_CODECS = ['G722', 'PCMU', 'PCMA'] as const;
export type SupportedCodec = (typeof SUPPORTED_CODECS)[number];

export interface SipRouteRule {
  uri: string;
  sip_address: string;
  port: number;
  codec?: SupportedCodec | null;
}

export interface SipRouteSnapshot {
  table_id: string;
  version: number;
  routes: Record<string, SipRouteRule>;
}
