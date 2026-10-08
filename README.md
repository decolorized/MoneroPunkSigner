# MoneroPunkSigner

**MoneroPunkSigner** is a fork of [Feather Wallet](https://github.com/feather-wallet/feather)
that works natively with **[ColdPunk](https://github.com/decolorized/ColdPunk)**, an air-gapped Monero cold
wallet on ESP32-S3 boards. Outputs, key images, unsigned and signed
transactions, and view-only wallet data travel over **USB HID**.

> [!WARNING]
> **Experimental. Not audited.** MoneroPunkSigner and ColdPunk have not been
> reviewed by third parties and have not been tested with real funds. Do not
> use them with money you cannot afford to lose.

Feather itself is a free, open-source Monero desktop wallet for Linux, Tails,
macOS and Windows, written in C++ with Qt. Everything Feather does,
MoneroPunkSigner does too; this document covers what the fork adds.

---

## Contents

- [What the fork adds](#what-the-fork-adds)
- [ColdPunk](#coldpunk)
- [Workflows](#workflows)
- [Seed restore: Feather and Cake/Cupcake passphrases](#seed-restore-feather-and-cakecupcake-passphrases)
- [Building](#building)
- [Device access (Linux udev)](#device-access-linux-udev)
- [Troubleshooting](#troubleshooting)
- [Source layout](#source-layout)
- [Related projects](#related-projects)
- [Upstream Feather resources](#upstream-feather-resources)
- [Disclaimer and license](#disclaimer-and-license)

---

## What the fork adds

| Area | Change |
| :--- | :--- |
| **HID transport** (`src/hid/`) | mwlink protocol 3 client over USB HID: framing with an 8-byte magic and header CRC, payload CRC32, 256 KiB limit, cancellation, device log forwarding. |
| **Offline signing wizard** (`src/wizard/offline_tx_signing/`) | A new method, **HID device**. Key images and signed transactions come back on the same page, with no import step. The Next/Finish button follows the selected method. |
| **Key image sync** | *Send via HID*: outputs go to the device, the user confirms there, and the returned key images are imported into the wallet. |
| **Signing** | *Sign on HID*: the unsigned transaction goes to the device, which shows the amount, fee and full addresses. The returned transaction must match the unsigned one (amount and fee) before it is offered for broadcast. |
| **Restore from keys** | A **ColdPunk** button in the button row of the page. It asks the device for the primary address, private view key, restore height and wallet name (Yes/No on the device) and fills the form. |
| **Encrypted polyseed** | 16-word seeds encrypted with a passphrase (Cake Wallet / Cupcake, polyseed "encrypted" flag) can be restored. The passphrase is asked for and the seed is decrypted. |
| **Safety around the device** | The device outbox is cleared before and after every exchange, so a stale result is never taken. Long operations run in a background thread and can be cancelled. Firmware with another protocol version is refused with a clear message. |
| **Optional build** | HID support is compiled in when `hidapi` is found. Without it the build is plain Feather and the HID options are hidden or disabled. |

---

## ColdPunk

ColdPunk is the firmware for the hardware side: an ESP32-S3 with a display (touch
or buttons) that keeps the seed, computes key images, and signs with CLSAG and
Bulletproofs+ after confirmation on its own screen. The seed, passphrase and
private spend key never leave the device. The private view key leaves only on a
PC request that you confirm on the device.

The firmware, supported boards, build instructions and the protocol
specification live in the **[ColdPunk repository](https://github.com/decolorized/ColdPunk)**: see its
[README](https://github.com/decolorized/ColdPunk#readme) and
[`tools/docs/usb_link_protocol.md`](https://github.com/decolorized/ColdPunk/blob/HEAD/tools/docs/usb_link_protocol.md).

USB identification: VID `0x303A`, PIDs `0x4024`, `0x4025`, `0x1001`, HID usage
page `0xFF00`. MoneroPunkSigner and the firmware must use the same protocol
version (currently **3**).

---

## Workflows

Unlock ColdPunk and **open the wallet on the device** before starting any of
these. Every result needs a *Yes* on the device.

### 1. Create the watch-only wallet

1. In the wallet wizard choose *Restore wallet from keys*, wallet type *View Only*.
2. Press **ColdPunk** and confirm on the device.
3. Address, view key, restore height and a wallet name (`<device wallet>_view_only`)
   are filled in. Continue as usual.

### 2. Sync key images (balance and spent outputs)

1. *Tools → Offline Transaction Signing* (or the sync prompt).
2. Step *1. Export outputs*: method **HID device**, optionally *Export all
   outputs*, then **Send via HID**.
3. Confirm on the device. The key images are imported automatically. When the
   wizard was opened only for syncing, *Finish* closes it.

### 3. Send

1. Create a transaction as usual. The watch-only wallet opens the offline
   signing wizard.
2. Step *3. Export unsigned transaction*: method **HID device**, **Sign on HID**.
3. Check the amount, the fee and **every full address on the device screen**,
   then sign there.
4. **View transaction** shows the signed transaction for review and broadcast.

Files still work: switch the method on any step.

---

## Seed restore: Feather and Cake/Cupcake passphrases

Two incompatible passphrase schemes exist for 16-word polyseeds. The format is
detected from the phrase itself:

| Phrase | What the passphrase does | In MoneroPunkSigner |
| :--- | :--- | :--- |
| Plain polyseed (Feather, ColdPunk) | seed offset applied to the derived key | *Options → Extend this seed with a passphrase* |
| Encrypted polyseed (Cake Wallet, Cupcake) | decrypts the phrase (`polyseed_crypt`), no offset | asked for automatically after the 16 words |

A wrong passphrase for an encrypted phrase cannot be detected (it derives a
different wallet), so compare the address with the original wallet. The wallet
keeps the phrase exactly as entered, so the seed dialog shows the encrypted
phrase, as Cupcake does.

---

## Building

Building follows upstream Feather; see [HACKING.md](HACKING.md) for a complete
development setup. HID support needs **hidapi** and **zlib** (CRC32), plus Qt6
Core and Concurrent.

### Linux

```sh
git clone --recurse-submodules https://github.com/decolorized/MoneroPunkSigner.git
cd MoneroPunkSigner
sudo apt install libhidapi-dev zlib1g-dev        # Debian / Ubuntu
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j"$(nproc)"
```

On Linux the `hidraw` backend (`hidapi-hidraw`) is preferred: no libusb, and
access is controlled by udev.

### macOS

```sh
brew install hidapi
```

Then build as on Linux. hidapi uses IOKit.

### Windows

Reproducible builds use Guix, as upstream:

```sh
./contrib/guix/guix-build x86_64-w64-mingw32
```

The result is in `guix/guix-build-*/output/x86_64-w64-mingw32/`. For a local
build, point CMake to hidapi with `HIDAPI_ROOT` (folder with
`include/hidapi/hidapi.h` and the library).

CMake reports `Feather: HID support enabled (feather_hid target found)` when
the HID module is built. Otherwise the HID options are unavailable at runtime.

---

## Device access (Linux udev)

Without a rule only root can open the HID device. Install one once:

```sh
sudo ./install-udev-rule.sh
```

It creates `/etc/udev/rules.d/70-moneropunksigner.rules` (VID `303a`, USB and
`hidraw`, `uaccess`), reloads udev and adds a `/dev/monero-punk-signer`
symlink. Re-plug the device afterwards.

---

## Troubleshooting

| Message | Meaning / fix |
| :--- | :--- |
| `HID device not found` | Cable is charge-only, the device is not unlocked yet (its USB starts only after the device password), or no udev rule on Linux. |
| `Device is locked` / `Wallet is not open on the device` | Unlock ColdPunk and open the wallet. |
| `The device is busy…` | Finish or cancel the pending screen on the device. |
| `Declined on the device` | *No* was pressed on the device. |
| Protocol version mismatch | Update the firmware and MoneroPunkSigner together. |
| `Outputs too large for HID` | More than 256 KiB: use files, or export only new outputs. |
| `amount or fee mismatch` | The device returned a signed transaction that does not belong to this one (stale outbox). Nothing is broadcast; retry. |
| `key image not synchronised` (device log) | Run key image sync with *Export all outputs*, then create the transaction again. |
| Restored address differs from Cupcake | Check the passphrase. A wrong passphrase derives another wallet. |

---

## Source layout

```
src/hid/                         mwlink client (MoneroPunkSigner addition)
  MwLink.h/.cpp                  frame format, constants, CRC, parsing
  MwHid.h/.cpp                   HID transport (64-byte reports, reassembly)
  MwWallet.h/.cpp                protocol client: info/status/put/get/clear/request,
                                 waitForResult, waitForRequest
  HidOperation.h/.cpp            background scenario with steps, cleanup, cancel
src/wizard/offline_tx_signing/   HID branches of the offline signing wizard
src/wizard/PageWalletRestoreKeys ColdPunk button (view-only data over HID)
src/wizard/PageWalletRestoreSeed encrypted polyseed restore
src/utils/Seed                   polyseed decryption before keygen
install-udev-rule.sh             Linux device access
```

---

## Related projects

| Project | Role |
| :--- | :--- |
| **[MoneroPunkSigner](https://github.com/decolorized/MoneroPunkSigner)** (this repository) | Feather Wallet fork for the PC: watch-only wallet with native ColdPunk support |
| **[ColdPunk](https://github.com/decolorized/ColdPunk)** | firmware of the offline signing device (ESP32-S3) |

Use matching versions: both sides must speak the same mwlink protocol version
(currently 3).

---

## Upstream Feather resources

- [Official site](https://featherwallet.org) · [Documentation](https://docs.featherwallet.org)
- [Upstream repository](https://github.com/feather-wallet/feather)
- [Matrix](https://matrix.to/#/#feather:monero.social) · IRC `#feather` on [OFTC](https://www.oftc.net/)
- How Feather is maintained: [MAINTENANCE.md](MAINTENANCE.md) · Releases: [RELEASE.md](RELEASE.md)

Questions about the HID transport, ColdPunk or anything specific to this fork
belong in **this** repository's issues, not in Feather's channels.

Security issues in this fork: open a private report in this repository.
Upstream Feather issues: see [SECURITY.md](SECURITY.md).

---

## Disclaimer and license

MoneroPunkSigner is an **unofficial fork**. It is not affiliated with, endorsed
by or supported by the Feather Wallet team, the Monero Project or Cake Wallet.
It is experimental and has not been audited.

Feather is free and open-source software licensed under
[BSD-3-Clause](LICENSE); this fork keeps the same license. Bundled third-party
components (Monero, polyseed and others) keep their own licenses.
