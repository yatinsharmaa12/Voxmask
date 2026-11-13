#!/usr/bin/env bash
# Local development only. Never changes default devices or restarts Core Audio.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
source_bundle="$repo_dir/build/macos/VoxMaskSilent.driver"
target=/Library/Audio/Plug-Ins/HAL/VoxMaskSilent.driver
expected_id=local.voxmask.prototype.silent
check_identity() {
  [[ ! -L "$1" ]] || { echo 'Refusing a symlink bundle' >&2; return 1; }
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$1/Contents/Info.plist")" == "$expected_id" ]] || {
    echo 'Unexpected bundle identity; refusing operation' >&2; return 1;
  }
}
case "${1:-status}" in
 status)
  if [[ -e "$target" ]]; then
    check_identity "$target"
    /usr/bin/codesign --verify --strict "$target"
    echo "Installed: $target (OS enumeration not checked by this command)"
  else
    echo 'Not installed'
  fi
  ;;
 install)
  [[ "$EUID" == 0 ]] || { echo 'Administrator privileges required: run this command with sudo.' >&2; exit 1; }
  [[ ! -e "$target" && ! -L "$target" ]] || { echo 'Destination already exists; refusing to replace it.' >&2; exit 1; }
  check_identity "$source_bundle"
  /usr/bin/codesign --verify --strict "$source_bundle"
  staging="$(/usr/bin/mktemp -d /Library/Audio/Plug-Ins/HAL/.voxmask-install.XXXXXX)"
  trap '/bin/rm -rf -- "$staging"' EXIT
  /usr/bin/ditto "$source_bundle" "$staging/VoxMaskSilent.driver"
  /usr/sbin/chown -R root:wheel "$staging/VoxMaskSilent.driver"
  /bin/chmod -R go-w "$staging/VoxMaskSilent.driver"
  /usr/bin/codesign --verify --strict "$staging/VoxMaskSilent.driver"
  /bin/mv -n "$staging/VoxMaskSilent.driver" "$target"
  [[ ! -e "$staging/VoxMaskSilent.driver" ]] || { echo 'Destination appeared during install; refusing overwrite.' >&2; exit 1; }
  echo "Installed: $target"
  echo 'Core Audio was not restarted. Check OS enumeration before using the device.'
  ;;
 uninstall)
  [[ "$EUID" == 0 ]] || { echo 'Administrator privileges required: run this command with sudo.' >&2; exit 1; }
  check_identity "$target"
  /bin/rm -rf -- "$target"
  echo 'Removed the VoxMask silent prototype files. Core Audio was not restarted.'
  ;;
 *) echo 'Usage: manage-macos-hal.sh status|install|uninstall' >&2; exit 2;;
esac
