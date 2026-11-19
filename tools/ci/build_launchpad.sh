#!/usr/bin/env bash
# Build merged ESP32-S3 images for the Thread Border Router Launchpad entries.
set -euo pipefail

variant=${1:?Usage: build_launchpad.sh 4mb|8mb|m5stack}
case "$variant" in
    4mb|8mb|m5stack) ;;
    *) echo "Unsupported flash variant: $variant" >&2; exit 1 ;;
esac
: "${IDF_PATH:?Export the ESP-IDF environment before running this script}"

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
br_revision=$(git -C "$repo_dir" rev-parse HEAD)
idf_revision=$(git -C "$IDF_PATH" rev-parse HEAD)
project_dir="$repo_dir/examples/basic_thread_border_router"
image_name="basic_thread_border_router_$variant"
if [ "$variant" = m5stack ]; then
    project_dir="$repo_dir/examples/m5stack_thread_border_router"
    image_name=m5stack_thread_border_router
fi
build_dir="$repo_dir/build/launchpad/$variant"
rcp_build_dir="$repo_dir/build/launchpad/rcp"
output_dir="$repo_dir/build/launchpad/images"
mkdir -p "$build_dir" "$rcp_build_dir" "$output_dir"

# Keep generated configs separate from the examples' development configs.
idf.py -C "$IDF_PATH/examples/openthread/ot_rcp" -B "$rcp_build_dir" \
    -D SDKCONFIG="$rcp_build_dir/sdkconfig" -D IDF_TARGET=esp32h2 build
printf 'CONFIG_RCP_SRC_DIR="%s"\n' "$rcp_build_dir" > "$build_dir/sdkconfig.rcp"
defaults="$project_dir/sdkconfig.defaults"
if [ "$variant" = 4mb ]; then
    defaults="$defaults;$project_dir/sdkconfig.defaults.4mb"
fi

idf.py -C "$project_dir" -B "$build_dir" \
    -D SDKCONFIG="$build_dir/sdkconfig" -D IDF_TARGET=esp32s3 \
    -D "EXTRA_COMPONENT_DIRS=$repo_dir/components/esp_ot_cli_extension;$repo_dir/components/esp_rcp_update" \
    -D "SDKCONFIG_DEFAULTS=$defaults;$build_dir/sdkconfig.rcp" \
    build
idf.py -C "$project_dir" -B "$build_dir" merge-bin \
    -o "$output_dir/$image_name.bin"

{
    printf 'esp-thread-br: %s\n' "$br_revision"
    printf 'esp-idf: %s\n' "$idf_revision"
} > "$output_dir/$image_name.txt"
