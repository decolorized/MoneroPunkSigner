#!/usr/bin/env bash
#
# install-udev-rule.sh — grants all users access to MoneroPunkSigner.
#
# Creates /etc/udev/rules.d/70-moneropunksigner.rules with MODE="0666"
# for VID 303a, reloads udev rules, and (optionally) creates a
# /dev/monero-punk-signer symlink.
#
# Usage: sudo ./install-udev-rule.sh

set -euo pipefail

# --- Settings ---
VENDOR_ID="303a"
RULE_FILE="/etc/udev/rules.d/70-moneropunksigner.rules"

# --- Root check ---
if [[ "$(id -u)" -ne 0 ]]; then
    echo "Error: run this script as root (sudo $0)" >&2
    exit 1
fi

echo "==> Installing udev rule for MoneroPunkSigner (VID ${VENDOR_ID})"

# --- Create rule file ---
cat > "${RULE_FILE}" <<EOF
# MoneroPunkSigner HID device — access for all users.
# Installed $(date -Iseconds)

# Main rule: access to all USB devices with VID 303a.
SUBSYSTEMS=="usb", ATTRS{idVendor}=="${VENDOR_ID}", MODE="0666", TAG+="uaccess"

# Additional: access to hidraw interfaces of the device.
KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="${VENDOR_ID}", MODE="0666", TAG+="uaccess"

# Optional: convenience symlink.
SUBSYSTEMS=="usb", ATTRS{idVendor}=="${VENDOR_ID}", SYMLINK+="monero-punk-signer"
EOF

echo "==> Created file: ${RULE_FILE}"
echo "--- Contents ---"
cat "${RULE_FILE}"
echo "----------------"

# --- Reload rules ---
echo "==> Reloading udev rules..."
udevadm control --reload-rules
udevadm trigger

echo "==> Rules reloaded."

# --- Verification ---
echo "==> Checking for devices with VID ${VENDOR_ID}..."
if lsusb -d "${VENDOR_ID}:" >/dev/null 2>&1; then
    echo "Found devices:"
    lsusb -d "${VENDOR_ID}:"
    echo ""
    echo "Checking permissions on /dev/hidraw*:"
    for f in /dev/hidraw*; do
        [[ -e "$f" ]] || continue
        echo "  $f: $(stat -c '%A %U:%G' "$f")"
    done
else
    echo "No devices with VID ${VENDOR_ID} found."
    echo "Connect MoneroPunkSigner and check again:"
    echo "  lsusb -d ${VENDOR_ID}:"
fi

echo ""
echo "==> Done. Unplug and replug the device."
echo "==> Feather should now work without root."