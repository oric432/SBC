#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
show_logs=false
test_args=()
for arg in "$@"; do
    if [[ "$arg" == "--show-logs" ]]; then
        show_logs=true
    else
        test_args+=("$arg")
    fi
done

run_dir="$(mktemp -d)"
log_dir="$run_dir/logs"
mkdir -p "$log_dir"
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
        docker compose -f "$root_dir/control-plane/backend/docker-compose.yml" down --volumes --remove-orphans || true
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
        if docker compose -f "$root_dir/control-plane/backend/docker-compose.yml" exec -T postgres \
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

docker compose -f "$root_dir/control-plane/backend/docker-compose.yml" up -d
compose_started=true
wait_for_postgres
export DATABASE_URL="postgresql://sbc:sbc@127.0.0.1:5432/sbc_control_plane"
(cd "$root_dir/control-plane/backend" && npm run db:push && npm run db:seed)

start_service backend bash -c "cd '$root_dir/control-plane/backend' && npm run dev"
start_service frontend bash -c "cd '$root_dir/control-plane/frontend' && npm run dev -- --host 127.0.0.1"
wait_for_http http://127.0.0.1:3001/api/b2bua/routes
wait_for_http http://127.0.0.1:5173

echo "UI: http://127.0.0.1:5173"
echo "Running SIPp suite against real control plane..."
just --justfile "$root_dir/engine/justfile" test-b2bua-stub --backend-demo "${test_args[@]}"
echo "Suite finished. Inspect call history in UI; press Ctrl-C to stop demo services."
wait
