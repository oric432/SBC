// Mirrors control-plane/backend/src/types/sipRegistrar.ts's UI-facing shape —
// never carries ha1, no codegen step, update this by hand if that changes.
export interface SipUser {
    id: number;
    username: string;
    realm: string;
    enabled: boolean;
}

export interface CreateSipUserPayload {
    username: string;
    realm: string;
    password: string;
    enabled: boolean;
}

export interface UpdateSipUserPayload {
    id: number;
    password?: string;
    enabled: boolean;
}
