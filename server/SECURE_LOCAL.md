# Secure local server and cloud compatibility

The same firmware works with the public Pi server or a local server. It stores
the server URL, device token, and optional private CA certificate in preferences.
They are never part of a firmware release or exposed by the health API.

For your public server, set the URL to `https://poko.example.com:443`, use
its device token, and leave the custom CA blank. The built-in CA bundle verifies
the certificate. Clock sync must complete before HTTPS succeeds.

For a local server, copy `server/config.secure-local.example.yml` to your private
config and change `poko.ip`, `poko.base_url`, `security.public_origin`, and media
folders. Use an IP address or hostname that the ESP32 can resolve. The HTTPS
certificate's SAN must match exactly. The example uses `192.168.0.5`.

Generate credentials (refuses to overwrite an existing directory):

```sh
python server/scripts/create_credentials.py server-secrets
```

On Linux/macOS protect the secrets directory and load the environment:

```sh
set -a
. ./server-secrets/credentials.env
set +a
```

On Windows use your account-only directory permissions and set the named
environment variables from the generated file in your service configuration;
do not check them into Git. The password recovery and device-token files are for
you, not public downloads.

The credential helper configures a private writable verifier file for password
changes from **Server Settings → Browser login password**. Run the server under
the account that owns that secrets directory. Browser login uses HttpOnly Secure
cookies over HTTPS, with optional **Remember me**; API clients can still use their
separate credentials. Server restarts require one fresh browser login.

For a small private installation, generate a local certificate with OpenSSL:

```sh
openssl req -x509 -newkey rsa:3072 -nodes -days 365 \
  -keyout server-secrets/server.key -out server-secrets/ca.pem \
  -subj '/CN=PoKo local' -addext 'subjectAltName=IP:192.168.0.5' \
  -addext 'basicConstraints=critical,CA:TRUE'
export POKO_TLS_CERT="$PWD/server-secrets/ca.pem"
export POKO_TLS_KEY="$PWD/server-secrets/server.key"
python -m server.poko_server --config server/config.yml
```

In PoKo's local dashboard, sign in using its **device-admin password**, then
save `https://192.168.0.5:8765` as the Media Server Address, the new local server's
device token, and the contents of `ca.pem` in the CA field. Trust that CA on your
browser machine to use the local server's UI without certificate warnings.
Never upload `server.key` to PoKo; it stays on the server. Never disable TLS
verification. Renew the certificate before expiry.

The local server uses the same API and streams as the public server; cloud
accounts and Cloudflare are not required. You can change back later without
reflashing. To clear an old private CA when switching to a public server, submit
an empty `ca` field in `POST /api/server`; to clear a token, submit an empty
`token` field. Empty fields in the dashboard preserve saved credentials.

Device-to-server catalog/control traffic uses HTTPS; server-to-device PCM/MJPEG
streams currently stay on your trusted LAN. Do not forward their TCP ports or
the device's HTTP control port to the internet. Remote devices behind another
NAT still require a routed private network or a future outbound streaming
protocol. The legacy unauthenticated LAN profile remains available for backwards
compatibility, but it is not the secure local profile described here.
