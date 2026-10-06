# MoneroPunkSigner

**MoneroPunkSigner** is a fork of [Feather Wallet](https://github.com/feather-wallet/feather)
with native support for the **MoneroPunkSigner** hardware cold wallet.

> ⚠️ **This project is for testing purposes only.**
> Do not use it with real funds. MoneroPunkSigner is an experimental
> fork and has not been audited. Use at your own risk.

This fork adds a HID transport to Feather so that Monero files
(outputs, key images, unsigned and signed transactions) can be exchanged
with the MoneroPunkSigner device over USB, without QR codes or file
transfer. The protocol used is **mwlink**, a compact binary framing
carried over USB HID reports.

Feather itself is a free Monero desktop wallet for Linux, Tails, macOS
and Windows. It is written in C++ with the Qt framework.

- **easy-to-use**, **small** and **fast** - Feather runs well on any modern hardware, including virtual machines and live operating systems.
- **beginner friendly**, but also caters to advanced Monero users by providing a [feature set](https://docs.featherwallet.org/guides/features) that is on par with the official CLI.
- ships with **sane defaults** that suit most users, but can also be configured for high or uncommon threat models.
- serves as a testing grounds for **experimental features** that may later be adopted in the reference wallets.

## MoneroPunkSigner features

Compared to upstream Feather, this fork adds:

- **HID transport** (`src/hid/`) — direct USB HID communication with the
  MoneroPunkSigner cold wallet, replacing QR codes and file transfer.
- **mwlink protocol** — binary exchange of outputs, key images, unsigned
  and signed transactions. Framing, CRC32, and command set match the
  reference `mwlink.py` implementation.
- **HID branches in the offline transaction signing wizard**
  (`src/wizard/offline_tx_signing/`) — a third method, **HID device**,
  appears alongside *Animated QR Codes* and *Files*.
- **Optional build** — HID support is compiled in automatically when
  `hidapi` is available. Without `hidapi`, the build proceeds as vanilla
  Feather and HID options are hidden.

The device is identified by USB VID `0x303A` and HID usage page `0xFF00`.

## Repository

Git repository: https://github.com/decolorized/MoneroPunkSigner

## Building

Building this fork follows the same steps as upstream Feather.

### Linux / macOS

    git clone --recurse-submodules https://github.com/decolorized/MoneroPunkSigner.git
    cd MoneroPunkSigner
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    cmake --build . -j $(nproc)

To build with HID support, install `hidapi`:

- **Ubuntu / Debian:** `sudo apt install libhidapi-dev`
- **macOS:** `brew install hidapi`

### Windows (cross-compilation via Guix)

    ./contrib/guix/guix-build x86_64-w64-mingw32

The resulting `feather.exe` (or installer) will appear in
`guix/guix-build-*/output/x86_64-w64-mingw32/`.

> **Note:** Windows builds are not officially supported by upstream
> Feather, but this fork builds them via Guix in the same way.

## Using MoneroPunkSigner

1. Connect the MoneroPunkSigner device over USB.
2. In Feather, open **Tools → Offline Transaction Signing**.
3. On the export outputs step, choose **HID device** as the method.
4. Press **Send via HID** and confirm the operation on the device.
5. Key images, unsigned and signed transactions are also transferred over HID.

## Resources

* [Official Site](https://featherwallet.org)
* [Documentation](https://docs.featherwallet.org)
* [Upstream Git Repository](https://github.com/feather-wallet/feather)
* [MoneroPunkSigner Git Repository](https://github.com/decolorized/MoneroPunkSigner)
* [Matrix](https://matrix.to/#/#feather:monero.social)
* IRC: `#feather` on [OFTC](https://www.oftc.net/)
* Mail: dev@featherwallet.org

If you need help with your wallet, please contact us via Matrix or IRC.
If you don't have an IRC client, you can join the room via [webchat](https://webchat.oftc.net/?randomnick=1&channels=feather).
If you don't receive a response immediately please idle in the room.

For issues specific to MoneroPunkSigner (HID transport, device
integration, this fork), please open an issue in this repository.

## Development

If you are looking to set up a development environment for Feather, see [HACKING.md](https://github.com/feather-wallet/feather/blob/master/HACKING.md).

It is highly recommended that you join our Matrix or IRC channel if you are hacking on Feather.
Idling in this channel is the best way to stay updated on best practices and new developments.

For information on how Feather is maintained, see: [MAINTENANCE.md](https://github.com/feather-wallet/feather/blob/master/MAINTENANCE.md)

To report a security vulnerability, see: [SECURITY.md](https://github.com/feather-wallet/feather/blob/master/SECURITY.md)

## Disclaimer

MoneroPunkSigner is an **unofficial fork** of Feather Wallet.
It is not affiliated with, endorsed by, or supported by the Feather
Wallet team. All questions about the HID transport and the
MoneroPunkSigner device should be directed to the maintainer of this
repository.

**This project is for testing purposes only.** It is experimental,
has not been audited, and must not be used with real funds.

Upstream Feather Wallet is licensed under **BSD-3-Clause**. This fork
keeps the same license.

## License

Feather is free and open-source software, [licensed under BSD-3](https://raw.githubusercontent.com/feather-wallet/feather/master/LICENSE).

