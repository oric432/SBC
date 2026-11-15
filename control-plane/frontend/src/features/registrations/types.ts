// Mirrors control-plane/backend's sip_registrations row shape — no codegen
// step, update this by hand if that schema changes.
export interface Registration {
    id: number;
    aor: string;
    contactUri: string;
    sourceAddress: string;
    sourcePort: number;
    transport: string;
    userAgent: string | null;
    expiresAt: string;
    updatedAt: string;
}
