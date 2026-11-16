#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
postgres_port="${SBC_DEMO_POSTGRES_PORT:-5432}"
backend_port="${SBC_DEMO_BACKEND_PORT:-3001}"
frontend_port="${SBC_DEMO_FRONTEND_PORT:-5173}"
backend_url="http://127.0.0.1:${backend_port}"
frontend_url="http://127.0.0.1:${frontend_port}"
export SBC_DEMO_POSTGRES_PORT="$postgres_port"
interactive=false
show_logs=false
test_args=()
for arg in "$@"; do
    case "$arg" in
        --interactive)
            interactive=true
            ;;
        --show-logs)
            show_logs=true
            ;;
        *)
            test_args+=("$arg")
            ;;
    esac
done

run_dir="$(mktemp -d)"
log_dir="$run_dir/logs"
mkdir -p "$log_dir"
compose_project="sbc-b2bua-demo-$$"
pids=()
declare -A service_pids=()
compose_started=false
cleanup_started=false

cleanup() {
    if "$cleanup_started"; then
        return
    fi
    cleanup_started=true
    trap '' INT TERM

    if [[ -n "${service_pids[engine]:-}" ]]; then
        kill "${service_pids[engine]}" 2>/dev/null || true
        wait "${service_pids[engine]}" 2>/dev/null || true
    fi
    for pid in "${pids[@]:-}"; do
        [[ "$pid" == "${service_pids[engine]:-}" ]] && continue
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    if "$compose_started"; then
        docker compose -p "$compose_project" -f "$root_dir/control-plane/backend/docker-compose.yml" down --volumes --remove-orphans || true
    fi
    rm -rf "$run_dir"
}
trap cleanup EXIT INT TERM

start_service() {
    local name="$1"
    shift
    "$@" >"$log_dir/$name.log" 2>&1 &
    pids+=("$!")
    service_pids["$name"]="$!"
    if "$show_logs"; then
        tail -f "$log_dir/$name.log" | sed -u "s/^/[$name] /" &
        pids+=("$!")
    fi
}

wait_for_http() {
    local url="$1"
    for _ in {1..50}; do
        if curl --silent --fail "$url" >/dev/null; then
            return
        fi
        sleep 0.2
    done
    echo "timed out waiting for $url" >&2
    return 1
}

wait_for_postgres() {
    for _ in {1..50}; do
        if docker compose -p "$compose_project" -f "$root_dir/control-plane/backend/docker-compose.yml" exec -T postgres \
            pg_isready -U sbc -d sbc_control_plane >/dev/null 2>&1; then
            return
        fi
        sleep 0.2
    done
    echo "timed out waiting for PostgreSQL" >&2
    return 1
}

command -v docker >/dev/null || { echo "docker is required" >&2; exit 1; }
command -v curl >/dev/null || { echo "curl is required" >&2; exit 1; }
[[ -x "$root_dir/engine/build/SbcEngine" ]] || { echo "build engine first: just engine build-dev" >&2; exit 1; }

docker compose -p "$compose_project" -f "$root_dir/control-plane/backend/docker-compose.yml" up -d
compose_started=true
wait_for_postgres
export DATABASE_URL="postgresql://sbc:sbc@127.0.0.1:${postgres_port}/sbc_control_plane"
export PORT="$backend_port"
export FRONTEND_URL="$frontend_url"
export VITE_BACKEND_URL="$backend_url"
(cd "$root_dir/control-plane/backend" && npm run db:push && npm run db:seed)

start_service backend bash -c "cd '$root_dir/control-plane/backend' && npm run dev"
wait_for_http "$backend_url/api/b2bua/routes"

if "$interactive"; then
    start_service frontend bash -c "cd '$root_dir/control-plane/frontend' && npm run dev -- --host 127.0.0.1 --port '$frontend_port'"
    wait_for_http "$frontend_url"
    echo "UI: $frontend_url"
fi

echo "Running SIPp suite against real control plane..."
just --justfile "$root_dir/engine/justfile" test-b2bua-stub \
    --backend-mode demo --live-backend-url "$backend_url" "${test_args[@]}"
if "$interactive"; then
    echo "Suite finished. Inspect call history in UI; press Ctrl-C to stop demo services."
    wait
fi
