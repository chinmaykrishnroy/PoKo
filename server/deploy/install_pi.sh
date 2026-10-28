#!/bin/sh
# Run from the deployed server/deploy directory with sudo. Idempotent: keeps
# existing credentials/config/media and never touches other services.
set -eu
id poko >/dev/null 2>&1 || useradd --system --home-dir /var/lib/poko --shell /usr/sbin/nologin poko
install -d -m 755 /srv/poko /srv/poko/music /srv/poko/movies
install -d -m 700 -o poko -g poko /var/lib/poko
install -d -m 700 -o poko -g poko /var/lib/poko/uploads
python3 /opt/poko/server/deploy/configure_pi.py
chown poko:poko /var/lib/poko/config.yml
install -m 644 /opt/poko/server/deploy/poko.service /etc/systemd/system/poko.service
systemctl daemon-reload
systemctl enable --now poko.service
