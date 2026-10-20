# License

This repository carries two licenses, following the upstream projects each part feeds into:

| Path | License | Why |
|---|---|---|
| `kernel/` (DTS, kernel patches, kernel config, audio topology) | **GPL-2.0-only** | Linux kernel sources; parts derived from Samsung's GPL SM-S901E kernel and the GPL-2.0 Galaxy Tab S8+ port |
| `phone-config/` (UCM2, modprobe, OpenRC/local.d, udev, NetworkManager snippets) | **MIT** | postmarketOS device configuration is conventionally MIT |
| `tools/`, `docs/`, `logs/`, `wifi-debug/`, top-level notes | **MIT** | Maximum reuse for other porters |

Full texts: [GPL-2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt) · [MIT](https://opensource.org/license/mit)

MIT License: Copyright (c) 2026 cammo1997

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## Not in this repository
Samsung's proprietary content (Wi-Fi/modem/ADSP firmware, `bdwlang.elf`, EFS/partition dumps, stock boot images) is
**never** included. `tools/s22-wifi-firmware-setup.sh` copies it from **your own phone**.
