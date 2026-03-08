# bambu-fw-fetch

## ⚠️ PRINTER WRITE PROTECTION — ABSOLUTE, NO EXCEPTIONS, NEVER BYPASSED

**NEVER execute, run, pipe input to, or interact with any command that sends a write or destructive operation to a physical printer — under any circumstances — without the user typing explicit permission in plain text in the current conversation turn.**

This means:
- **Do NOT run `bambu_fw_upgrade.py`** (or any variant) without the user saying so explicitly in their message.
- **Do NOT use `write_bash`, `echo "YES" |`, or any other mechanism to answer a confirmation prompt on behalf of the user.** The confirmation prompt exists for humans only.
- **Do NOT run it as a "test", "dry-run validation", "connection check", or any other pretext.** `--dry-run` is safe; anything else is not.

Prohibited operations (not exhaustive):
- Firmware update commands (`upgrade.start`, any `upgrade.*`)
- MQTT publish to any `device/*/request` topic
- GCode commands
- Configuration changes
- Any FTP/file upload to a printer

This rule applies in **all operating modes** without exception: interactive, autopilot, background agents, scripted execution. Violating this rule has already caused accidental firmware flash attempts on a physical printer twice. There will be no third time.

---

> **Global rules** in `~/.copilot/copilot-instructions.md` are always in effect. This file extends them with project-specific guidance.

Single-file C++ utility that calls `libbambu_networking.dylib` (Bambu Lab's
closed-source network plugin) to retrieve firmware download URLs for a Bambu
Lab printer via the cloud API.

## Build

```bash
make
```

## Run

```bash
./bambu_fw_fetch <DEV_ID>    # fetch firmware for a specific cloud-bound device
./bambu_fw_fetch --info      # call get_user_print_info (no device binding required)
```

`DEV_ID` is the printer serial number. The tool reads BambuStudio's cached
login session from `~/Library/Application Support/BambuStudio/` — BambuStudio
must be logged in at least once for the token to be present.

**Known printer serials:**
- H2D: `0948AD5B2700913`
- A1:  `03919A3B2000654`

## Prerequisites (cloud binding required)

`get_printer_firmware` returns `"devices":null` for printers in **LAN-only mode**.
The API is device-bound — it looks up firmware for a specific printer registered
under the cloud account. Printers must be temporarily bound to the Bambu cloud
account in BambuStudio before the firmware URL can be retrieved.

To get a firmware URL:
1. Open BambuStudio → device → connect via cloud mode
2. Run `./bambu_fw_fetch <SERIAL>`
3. Extract the signed CDN URL from `firmware[*].url` in the JSON output
4. `curl -L "<url>" -o firmware.zip`
5. Switch back to LAN-only mode if desired

## Dependencies

- macOS only
- BambuStudio installed at `/Applications/BambuStudio.app`
- `~/Library/Application Support/BambuStudio/plugins/libbambu_networking.dylib`

## libbambu_networking.dylib — Verified Function Signatures

All signatures verified from `bambulab/BambuStudio` `NetworkAgent.hpp` (master).

```cpp
// Init sequence — must run in this exact order
void* bambu_network_create_agent(std::string log_dir)
int   bambu_network_set_config_dir(void* agent, std::string config_dir)
int   bambu_network_init_log(void* agent)
int   bambu_network_set_cert_file(void* agent, std::string folder, std::string filename)
int   bambu_network_set_country_code(void* agent, std::string country_code)
int   bambu_network_start(void* agent)

// Session
bool  bambu_network_is_user_login(void* agent)
int   bambu_network_destroy_agent(void* agent)   // MANDATORY before process exit

// Data queries (use cached auth internally — no token parameter)
int   bambu_network_get_printer_firmware(void* agent, std::string dev_id,
                                         unsigned* http_code, std::string* http_body)
int   bambu_network_get_user_print_info(void* agent,
                                        unsigned* http_code, std::string* http_body)

// NOT a cached-token getter — exchanges an OAuth ticket for a new token:
int   bambu_network_get_my_token(void* agent, std::string ticket,
                                 unsigned* http_code, std::string* http_body)
```

## Critical Implementation Notes

**`destroy_agent` is mandatory** — calling it before process exit is not optional.
Without it, library background threads throw an uncaught exception during process
cleanup → `libc++abi: terminating` abort. Call it on every exit path.

**Log dir pre-creation** — `create_agent(log_dir)` internally calls
`boost::filesystem::create_directory(log_dir + "/log")`. If `log_dir` itself
does not exist, boost throws `filesystem_error` (uncaught → abort). Pre-create
both `log_dir` and `log_dir/log` with `mkdir()` before calling `create_agent`.

**`get_my_token` is NOT a token getter** — its signature is
`int (void* agent, std::string ticket, ...)`. It exchanges a short-lived OAuth
ticket/code for a session token. Calling it as `std::string (void* agent)` causes
a segfault due to ABI mismatch. There is no exported function that returns the
currently cached token as a plain string.

**`start()` restores cached session** — after `set_config_dir` + `start()`,
`is_user_login()` returns true if the BambuStudio-cached token is still valid.
Token is stored in the encrypted `BambuNetworkEngine.conf`; not directly readable.

**`sleep(1)` after `start()`** — the library spawns background init threads.
Wait at least 1 second before calling `is_user_login()` or any data function.

**Country code** — use `"US"` (North America region; confirmed from BambuStudio.conf).

**Cert path** — `set_cert_file(agent, "/Applications/BambuStudio.app/Contents/Resources/cert", "slicer_base64.cer")`

**Config dir** — `~/Library/Application Support/BambuStudio/`

## Known API Behavior

| Call | Result when printers are LAN-only |
|------|-----------------------------------|
| `get_printer_firmware(agent, "<serial>", ...)` | HTTP 200, `"devices":null` |
| `get_printer_firmware(agent, "", ...)` | returns -11 (invalid argument) |
| `get_user_print_info(agent, ...)` | HTTP 200, `{"message":"success","code":0,"error":null}` (no device data) |

## Offline Firmware Packages

### Fetching the download URL (Cloudflare bypass)

`curl` is blocked by Cloudflare on Bambu's firmware download page. Use **Python `requests`** instead:

```python
import requests, re
r = requests.Session().get(
    'https://bambulab.com/en/support/firmware-download/{model}',   # model = a1, h2d, etc.
    headers={'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36'},
    timeout=20
)
urls = re.findall(r'https?://[^\s"\'<>]*offline[^\s"\'<>]*\.zip', r.text)
```

This returns all offline zip URLs for the requested model. The page sometimes returns 403; retry once if needed.

### URL format

```
https://public-cdn.bblmw.com/upgrade/device/offline/{MODEL}/{VERSION}/{HASH}/offline-ota-{model_lower}_{VERSION}-{STAMP}.zip
```

Examples:
- `https://public-cdn.bblmw.com/upgrade/device/offline/N2S/01.07.02.00/62201b7e2c/offline-ota-n2s_v01.07.02.00-20260113213823.zip`
- `https://public-cdn.bblmw.com/upgrade/device/offline/O1D/01.02.00.00/9b1c10b10e/offline-ota-h2d_v01.02.00.00-20250813144019.zip`

CDN files are NOT guessable — the `{HASH}` is a per-build identifier and the URLs require no auth token once you have the exact path. Use the Python scraper above to get the correct URL.

### Known model codes

| Printer | Model code | Latest offline firmware |
|---------|-----------|------------------------|
| A1      | N2S       | `01.07.02.00` |
| A1 Mini | N1        | `01.07.02.00` |
| H2D     | O1D       | `01.02.00.00` (newer versions exist via cloud-push only) |
| H2D Pro | O1E       | `01.01.00.00` |
| P1S     | C12       | — |
| P1P     | C11       | — |

### Offline zip structure

**A1 (N2S):** Flat zip, all files at root. Core files are individual `.bin.sig` components. Main manifest: `ota-n2s_v{VERSION}-{STAMP}.json.sig`. Package index: `ota-package-list.json.sig`.

**H2D (O1D):** Similar flat structure but with key differences:
- Main firmware is a 500+ MB **full Linux image** (`update-v{ver}-{stamp}_product.img.sig`) — not just component bins.
- Core component URLs in the manifest are `http://127.0.0.1/<filename>` — the printer serves the zip contents via a local HTTP server during SD card upgrade.
- Accessory firmware (AMS, toolheads, laser cutter modules) uses `http://upgrade.bambooolab.com/...` (3 o's — an internal Bambu CDN).
- Package index: `ota_package-list.json.sig` (note underscore, vs A1's dash).

### Cannot construct offline zips for unreleased versions

All `.json.sig` and `ota_package-list.json.sig` files carry a **BIMH RSA-2048 signature** (magic bytes `42 49 4d 48` = `BIMH`). The printer verifies this signature before applying any update. **Bambu's BIMH firmware signing private key has NOT been leaked** (the January 2025 leak was the Bambu Connect X.509 MQTT authentication key — a different key; see below). It is not possible to construct a valid offline zip for firmware versions that Bambu has not published an offline package for.

### MQTT upgrade limitations (LAN mode)

`bambu_fw_upgrade.py` sends `upgrade_confirm` (src_id=2, upgrade_type=4, module=ota) with the manifest URL. This works **only when the printer has `new_ver_list` pre-populated by the cloud** — i.e., the printer previously received a cloud update notification. On printers in permanent LAN mode with empty `new_ver_list`, `upgrade_confirm` is accepted (echoed with result=success) but immediately transitions to `UPGRADE_FAIL / dis_state: 3`. The printer does not even attempt to fetch the manifest URL in this state.

**SD card upgrade is the reliable LAN-mode path.** The printer's SD card is accessible via SFTP — upload the offline zip directly without physically removing the card:
```
sftp <printer-ip>
put /tmp/offline-ota-n2s_v01.07.02.00-20260113213823.zip /sdcard/
```
Then trigger the upgrade from the printer touchscreen (Settings → Firmware).

### Bambu key leak clarification

Two distinct key categories exist:

| Key | Status | Purpose |
|-----|--------|---------|
| Bambu Connect X.509 certificate + private key | **Leaked Jan 2025** (extracted from Bambu Connect Electron `main.js`) | Authenticates MQTT connections; used by LAN-mode tools and our containers |
| BIMH RSA firmware signing key | **Not leaked** | Signs `.bin.sig` / `.img.sig` / `ota_package-list.json.sig` files |

The leaked auth key is already in use (it enables all LAN MQTT access). It does not enable firmware signing.

## Git Workflow (Mandatory)

- **Never run `git commit` or `git push`** on behalf of the user. No exceptions.
- Stage files and run pre-commit hooks as needed; stop short of committing.
- Report what is staged and ready.
