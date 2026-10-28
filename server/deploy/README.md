# Persistent Pi deployment

The installed service is `poko.service`; the origin is **127.0.0.1:8765**.
It runs as the unprivileged `poko` account, restarts on failure, starts at boot,
and exposes no listener on the Pi's LAN or public interfaces.

## Cloudflare Tunnel

On the tunnel connector running on this Pi, add a public hostname:

- Hostname: `poko.prefect-sys.online`
- Service: `http://127.0.0.1:8765`
- Cache: bypass for all paths (the application also sends `Cache-Control: no-store`).

Do not use HTTPS for the loopback origin: Cloudflare terminates public HTTPS.
If cloudflared runs in a container, its `127.0.0.1` is a different namespace;
use host networking or put the origin and connector on the same private network.
Do not solve that by exposing the unauthenticated legacy server.

Add a Cloudflare Access policy restricted to your email for an additional login
and rate-limit layer. The origin still independently requires authentication.
Do not publish the ESP32 dashboard, OTA handler, Snapcast, or TCP media ports.

## Sign in

Username: `admin`. Retrieve the generated password on the Pi:

```sh
sudo cat /etc/poko/admin-password
```

The browser uses its native sign-in prompt. Use the public HTTPS hostname.
For local testing, use an SSH local forward rather than opening port 8765 to
the LAN. Browser Basic credentials are cached by the browser; close its private
session to sign out. This is a single-administrator deployment, not multi-user
accounts or an account-isolated media hosting service.

Only root can read `/etc/poko/credentials.env` and the password recovery file.
The server gets only a salted scrypt verifier through its service environment;
passwords are absent from YAML, HTTP configuration responses, and Git.
To rotate, run a root shell, remove those two files, run `configure_pi.py`, and
restart the service. Existing browser credentials then stop working.

## Media and state

- Add music to `/home/roy/Music`.
- Add movies to `/home/roy/Movies` (created if previously absent).
- Inside the sandbox these are mounted read-only at `/srv/poko/music` and
  `/srv/poko/movies`.
- Editable configuration: `/var/lib/poko/config.yml`.
- Database: `/var/lib/poko/catalog.db`.
- Writable runtime/uploads: `/var/lib/poko/uploads`.

Use **Rescan** in the library after copying media. Security settings require a
restart and cannot be disabled using the web config editor. Existing config
and credentials are preserved on reinstall. The service deliberately cannot
write to the original media folders or read the rest of `/home/roy`.

## Operations

```sh
sudo systemctl status poko
sudo journalctl -u poko -n 50 --no-pager
sudo systemctl restart poko
sudo python3 /opt/poko/server/deploy/verify_pi.py
```

The application has eight concurrent request slots and 15-second socket
timeouts. systemd limits memory, processes, file descriptors, and privileges;
FFmpeg child processes are killed together with the service on restart. Back up
`/var/lib/poko/config.yml` and the credentials separately from the media itself.

## Transport limitations

The internet hostname securely exposes the server's administration/catalog API.
Firmware v2 uses a separate bearer device token for its HTTPS catalog and media-control requests. The admin browser password is never provisioned to the device. Device tokens cannot read configuration, access device administration, or change server settings. There is no anonymous bypass.

The server can control and stream to a configured device on a network reachable
from the Pi. It sends PCM/MJPEG to device TCP ports 1234–1237; an HTTP tunnel
does not make a remote ESP32 behind NAT reachable. In secure mode playback
always targets the configured device, never the Cloudflare proxy's source IP.
Do not trust X-Forwarded-For as a media destination or authentication signal.

Supporting a travelling device requires a device-initiated authenticated TLS
stream (and provisioned per-device credentials), or a private routed VPN gateway.
That is a firmware/protocol migration; public reverse TCP ports are not a safe
replacement. HTTPS firmware must validate certificates; do not use setInsecure.

Firmware v2 protects device control and web OTA using Digest authentication, and ArduinoOTA is enabled only after a device-admin password exists. Provision on a trusted LAN using `sudo python3 /opt/poko/server/deploy/pair_device.py 192.168.0.3` within five minutes after the first v2 boot. The local root dashboard also presents an initial password form during this window. All other unpaired device routes are blocked. Reboots do not clear provisioned credentials.

The device username is `admin`; its separate password is in `/etc/poko/device-admin-password`. The media token is in `/etc/poko/device-token`. Changing the browser password does not break device playback. Keep raw media ports and the device dashboard LAN-only; Digest authenticates but does not encrypt device HTTP control. Firmware signatures, encrypted raw media transport, encrypted NVS and secure boot are not implemented by this release.
