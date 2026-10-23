#!/usr/bin/env bash
# Repackages a mesh-fire-sensor build under the rak4631 board name, because the
# Meshtastic web flasher looks files up as firmware-<board>-<version>.<ext>.
set -euo pipefail

if [[ $# -ne 3 ]]; then
	echo "Usage: $0 <version> <build-dir> <out-dir>" >&2
	exit 1
fi

version="$1"
build_dir="$2"
out_dir="$3"
board="rak4631"
src="firmware-mesh-fire-sensor-${version}"
dst="firmware-${board}-${version}"

mkdir -p "${out_dir}"
cp "${build_dir}/${src}.uf2" "${out_dir}/${dst}.uf2"
cp "${build_dir}/${src}.hex" "${out_dir}/${dst}.hex"
cp "${build_dir}/${src}.zip" "${out_dir}/${dst}-ota.zip"

jq --arg src "${src}" --arg dst "${dst}" --arg board "${board}" '
	.platformioTarget = $board
	| .files |= map(select(.name | endswith(".elf") | not) | .name |= sub("^" + $src; $dst))
' "${build_dir}/${src}.mt.json" >"${out_dir}/${dst}.mt.json"

jq -n --arg version "${version}" --arg board "${board}" \
	'{version: $version, targets: [{board: $board, platform: "nrf52840"}]}' \
	>"${out_dir}/firmware-${version}.json"

(
	cd "${out_dir}"
	zip -q "mfd-${board}-sensor-${version}.zip" "${dst}.uf2"
	if command -v sha256sum >/dev/null; then
		sha256sum -- * >SHA256SUMS
	else
		shasum -a 256 -- * >SHA256SUMS
	fi
)
