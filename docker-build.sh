#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

artifacts=(
  out/gba-sd-card-info.gba
  out/gba-sd-card-info.elf
  out/gba-sd-card-info.map
  out/superchis-sd-info.gba
  out/superchis-sd-info.elf
  out/superchis-sd-info.map
  out/source.tar.gz
  out/README.md
  out/LICENSE
  out/THIRD_PARTY.md
)

build_image() {
  local build_commit build_tag
  if build_commit=$(git rev-parse --short=8 HEAD 2>/dev/null); then
    if ! git diff --quiet HEAD --; then
      build_commit+=-dirty
    fi
    build_tag=$(git describe --tags --exact-match HEAD 2>/dev/null || true)
  else
    printf '%s\n' 'Commit the project before building so the report can identify this build.' >&2
    return 1
  fi
  : > build.log
  mkdir -p out
  rm -f -- "${artifacts[@]}"
  sudo docker compose build --pull --build-arg "BUILD_COMMIT=$build_commit" \
    --build-arg "BUILD_TAG=$build_tag" "$@" builder 2>&1 | tee -a build.log
  sudo docker compose run --rm --no-deps --user "$(id -u):$(id -g)" builder \
    2>&1 | tee -a build.log
}

if (( $# > 1 )); then
  printf '%s\n' 'Only one command is accepted.' >&2
  exit 2
fi
case "${1:-build}" in
  build) build_image ;;
  rebuild) build_image --no-cache ;;
  clean) rm -f -- "${artifacts[@]}" build.log ;;
  help) printf '%s\n' 'Usage: ./docker-build.sh [build|rebuild|clean|help]' ;;
  *) printf 'Unknown command: %s\n' "$1" >&2; exit 2 ;;
esac
