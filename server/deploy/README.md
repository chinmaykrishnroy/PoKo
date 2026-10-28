# Raspberry Pi deployment: a reference architecture

This case study describes one way to run an always-on PoKo media server. It is
an adaptable example for self-hosters, not a hosted service, a required platform,
or a description of a maintainer's private installation. Hostnames, device
addresses, accounts, and media paths below are examples; choose your own.

For a server used only at home, see [secure local hosting](../SECURE_LOCAL.md).
Cloudflare and an internet domain are optional. Both approaches use the same
firmware, API, and separate browser and device credentials.

## The pattern

```text
Browser / PoKo HTTPS requests
             |
    Your HTTPS hostname
             |
  Cloudflare Tunnel connector
             |
      127.0.0.1:8765
             |
    Unprivileged PoKo service
             |
  Read-only media / private state
```

The example systemd service starts at boot and restarts on failure. Its HTTP
origin listens only on loopback. The tunnel handles public TLS, while PoKo
independently authenticates requests. No router port forwarding is needed.

The Pi must also be able to reach the device on its trusted LAN: the current
media transport sends PCM/MJPEG to the device's TCP ports 1234–1237. Publishing
the server's HTTPS API does **not** make an ESP32 behind a remote NAT reachable.
Travelling devices need a private routed VPN gateway or a future device-initiated
TLS media transport. Never publish the device dashboard, OTA, Snapcast, or raw
media ports to the internet.

## Adapt the example

The files in this directory illustrate a Linux/systemd installation. Review and
adapt them before running with root privileges. They assume the server package
has been copied to `/opt/poko/server`, Python dependencies from
`server/requirements.txt` are installed for the service interpreter, and FFmpeg
is installed. If using a virtual environment, adapt `ExecStart` in the unit.

1. Choose a hostname you control, for example `poko.example.com`, and reserve
   your device's LAN IP in DHCP. Example device addresses are not defaults for
   anyone else's hardware.
2. Edit `poko.service`: replace the left sides of `BindReadOnlyPaths` with your
   existing music and movie directories. The sample uses `/srv/media/music` and
   `/srv/media/movies`; create a movie directory if needed. The service sees
   read-only mounts at `/srv/poko/music` and `/srv/poko/movies`. Ensure the `poko`
   account can read the files and traverse their parent directories. Keep media
   outside the source checkout.
3. On first initialization, pass your own origin and device address:

   ```sh
   sudo sh /opt/poko/server/deploy/install_pi.sh \
     --public-origin https://poko.example.com --device-host 192.168.1.50
   ```

   The script creates a dedicated `poko` account and private state, installs the
   adapted unit, and enables the service. Existing configuration and credentials
   are preserved; later command-line arguments do not overwrite them. Edit the
   private config explicitly when changing an existing installation.
4. Configure your tunnel to route your hostname to `http://127.0.0.1:8765`.
   Bypass caching; the origin also sends `Cache-Control: no-store`. If the tunnel
   connector is containerized, its loopback is a different network namespace:
   use host networking or a private network shared with the origin. Keep the
   origin inaccessible from public interfaces.
5. Provision the device on your trusted LAN within its initial five-minute
   pairing window:

   ```sh
   sudo python3 /opt/poko/server/deploy/pair_device.py 192.168.1.50 \
     --server-url https://poko.example.com
   ```

   The helper sends an independent device-admin password and media token. It
   does not print either secret. For a private-CA local server, provision its CA
   in the device dashboard using the [local hosting guide](../SECURE_LOCAL.md).
6. Copy the repository's `.env.example` to a private `.env` file and fill in your
   own device/server/origin URLs, credential-file directory, and expected firmware
   version. No passwords need to be copied into it: verification reads the private
   files at `POKO_TEST_SECRETS_DIR`. Then verify the boundaries:

   ```sh
   sudo python3 /opt/poko/server/deploy/verify_pi.py --env /path/to/private.env
   sudo python3 /opt/poko/server/deploy/verify_public.py --env /path/to/private.env
   sudo python3 /opt/poko/server/deploy/verify_device.py --env /path/to/private.env
   sudo python3 /opt/poko/server/deploy/verify_browser.py --env /path/to/private.env
   ```

The public check expects anonymous requests to be rejected, valid browser and
media credentials to work, and media credentials to be denied configuration.
The device check verifies firmware and server selection. Set `POKO_TEST_PLAYBACK=1`
to opt into Music/Video checks; these stop current playback, lower test volume,
and restore the previous app and master volume afterward. Supply a song and a
selected video longer than 30 seconds. The scripts do not create media. Process
environment variables override `.env` values. `.env` is ignored by Git; the
example contains illustrative targets only. Hermetic unit tests keep synthetic
fixture values and do not require access to your deployment.
The browser check verifies login, parallel tab requests and logout without native
HTTP-auth prompts. Set `POKO_TEST_REBOOT=1` only when a device reboot is acceptable
to additionally verify remembered login after reboot and clock sync.

Optional Cloudflare Access policies must cover only browser/admin routes such
as `/`, `/api/server/*`, and `/api/device/*`. Keep interactive challenges off
`/health`, `/api/library/*`, `/api/audio/*`, `/api/video/*`, `/api/image/*`, and
`/api/playback/*`: the ESP32 cannot complete a browser login. These routes still
require origin authentication with a restricted device token. Additional
Cloudflare service-token support would be needed before protecting the entire
hostname with Access.

## Credentials and password changes

This is a single-owner installation, not a multi-user media hosting platform.
There are distinct credentials:

| Credential | Purpose | Initial private file |
| --- | --- | --- |
| Server browser login (`admin`) | Manage the media server | `/etc/poko/admin-password` |
| Media device token | Device catalog and playback requests over HTTPS | `/etc/poko/device-token` |
| Device administrator (`admin`) | Local device dashboard, configuration and OTA | `/etc/poko/device-admin-password` |

Root-only `/etc/poko/credentials.env` provides the server's scrypt password
verifier, token hash, and device-control credential. Secrets must not be stored
in Git, editable YAML, screenshots, or public configuration responses.

Retrieve the server browser password locally as the operator:

```sh
sudo cat /etc/poko/admin-password
```

Rotate **only** that browser password without disturbing device playback:

```sh
sudo python3 /opt/poko/server/scripts/rotate_admin.py
sudo systemctl restart poko
```

Do not delete the whole credentials file to rotate one password; that can break
pairing.
Use the login form's **Remember me** to keep a browser session across tabs and
browser reopenings for up to 30 days. Server sessions are kept in bounded RAM;
restarting the service requires signing in once again. **Sign out** revokes the
current session. Password changes revoke all browser sessions. Programmatic
Basic/Bearer authentication remains available independently of browser sessions.
Use the HTTPS public/local address for browser login; origin HTTP is reserved
for loopback API checks. Do not expose plaintext Basic authentication on LAN.

Device firmware v2.0.2 adds **Web UI → Settings → Security** for human password
changes and **Settings → Security** on the physical device for confirmed
recovery. Firmware v2.1.0 also lets the owner **show the existing saved password**
on that physical screen; viewing it does not change it or disconnect clients.
Remembered device cookies survive reboots when clock sync is available. After a
change, the initially provisioned device-admin recovery file is stale: save the
new password in your password manager. Reboot to update ArduinoOTA's password.

On the first device password change, the existing Pi credential becomes a
separate, restricted integration credential for media controls. It cannot open
the dashboard, change security/server settings, reset drivers, or upload OTA.
Further human password changes preserve it, so the Pi remains paired. This does
not revoke a compromised integration credential; replace/re-provision that
credential separately if needed. Device HTTP control remains LAN-only: Digest
authenticates requests but does not encrypt the connection.

For an easier-to-type server password, use **Server Settings → Browser login
password**, or choose one privately with:

```sh
sudo python3 /opt/poko/server/scripts/rotate_admin.py --prompt
sudo systemctl restart poko
```

The tool also supports `--word-list /path/to/words.txt` to generate six random
words from a list of at least 4096 distinct words. It never prints the password.
The generated initial/recovery password is in the operator's private file; after
choosing a new password in the Web UI, save it in your password manager because
that initial file is stale. An operator CLI reset restores access if forgotten.
`POKO_ADMIN_PASSWORD_HASH_FILE` names a private writable verifier file, outside
editable YAML. Pi setup places it in `/var/lib/poko/admin-verifier.json`; local
credential setup places it in the private secrets directory. Only salted scrypt
verifiers are written there. An operator rotation of the base environment hash
invalidates an old browser override, so CLI recovery always takes precedence.

## Private state and operation

Example paths can be changed together in the configuration and service unit:

- `/var/lib/poko/config.yml`: editable server configuration.
- `/var/lib/poko/catalog.db`: media index.
- `/var/lib/poko/uploads`: writable runtime uploads.
- `/srv/poko/music` and `/srv/poko/movies`: read-only source media mounts.

Use **Rescan** after copying media. Security configuration changes require a
service restart and cannot be disabled from the web config editor. Back up
private configuration and credentials separately from source media.

```sh
sudo systemctl status poko
sudo journalctl -u poko -n 50 --no-pager
sudo systemctl restart poko
```

The example limits HTTP concurrency, socket timeouts, memory, processes, file
descriptors, and privileges. FFmpeg children stop with the service. Tune resource
limits for your machine and workload; an example configuration is not a capacity
guarantee. Check free disk space before indexing or copying large libraries.

TLS verification uses the device's CA bundle or a provisioned private CA and
requires a valid device clock. Do not disable certificate verification. Secure
boot, signed firmware, encrypted NVS, and encrypted raw LAN media transport are
not implemented by this release. A tunnel alone does not add those properties.
