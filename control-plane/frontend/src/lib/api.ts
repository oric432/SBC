// Mirrors backend src/utils/apiResponse.ts — the response envelope every
// endpoint returns.
export interface ApiSuccess<T> {
    success: true;
    data: T;
}

export interface ApiErrorBody {
    success: false;
    error: {
        message: string;
        details?: unknown;
    };
}

export type ApiResponse<T> = ApiSuccess<T> | ApiErrorBody;

const FALLBACK_ERROR_MESSAGE = "Something went wrong, try again later";
const BACKEND_UNREACHABLE_MESSAGE = "Can't reach the backend — make sure it's running";

// RTK Query's fetchBaseQuery puts the parsed JSON body on `error.data` and
// the HTTP status on `error.status`. Pull the backend's message back out.
export function getApiErrorMessage(error: unknown): string {
    if (error && typeof error === "object" && "status" in error) {
        const status = (error as { status: unknown }).status;
        // When the backend process isn't up, Vite's dev proxy (or a prod
        // reverse proxy) responds with a non-JSON error page instead of our
        // envelope — fetchBaseQuery can't parse that, so it reports
        // FETCH_ERROR/PARSING_ERROR rather than a real backend status.
        if (status === "FETCH_ERROR" || status === "PARSING_ERROR") {
            return BACKEND_UNREACHABLE_MESSAGE;
        }
    }

    if (
        error &&
        typeof error === "object" &&
        "data" in error &&
        error.data &&
        typeof error.data === "object" &&
        "error" in error.data
    ) {
        const body = (error as { data: ApiErrorBody }).data;
        if (body.error?.message) return body.error.message;
    }
    return FALLBACK_ERROR_MESSAGE;
}
