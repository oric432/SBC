mod engine "engine"
mod backend "control-plane/backend"
mod frontend "control-plane/frontend"

# List all available root commands and provide help
default:
    @just --list
    @echo ""
    @echo "Tip: Run 'just <component> run-dev' to run a specific component (e.g. 'just engine run-dev')"
    @echo "Tip: Run 'just <component> --list' to see component-specific commands (e.g. 'just backend --list')"

# Setup and install dependencies for all components
setup-all:
    just engine setup
    just backend setup
    just frontend setup

# Build all components in dev mode
build-dev-all:
    just engine build-dev
    just backend build-dev
    just frontend build-dev

# Build all components in release mode
build-release-all:
    just engine build-release
    just backend build-release
    just frontend build-release

# Run all components concurrently in dev mode (with a 3s delay for the engine)
run-dev-all:
    just backend run-dev & just frontend run-dev & (sleep 10 && just engine run-dev) & wait

# Run all components concurrently in release mode (with a 3s delay for the engine)
run-release-all:
    just backend run-release & just frontend run-release & (sleep 6 && just engine run-release) & wait

# Run tests for all components
test-all:
    just engine test
