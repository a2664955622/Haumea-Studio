# Deploying the telemetry and crash report server

Haumea Studio's daily pings go to `https://telemetry.itsneufox.xyz/haumea/telemetry/ping`
and its crash reports to `https://reports.itsneufox.xyz/haumea/reports`. This
folder is the server for both: one small Python service (standard library
only, about 30 MB of memory) behind Caddy, which handles HTTPS. These steps
are for Debian or Ubuntu on the VPS that already serves itsneufox.xyz.

## 1. DNS

At your DNS provider, add records pointing both names at the VPS:

| Name | Type | Value |
|---|---|---|
| `telemetry` | A | the VPS's IPv4 address |
| `reports` | A | the VPS's IPv4 address |
| `telemetry` | AAAA | the VPS's IPv6 address (if it has one) |
| `reports` | AAAA | the VPS's IPv6 address (if it has one) |

Check with `dig +short telemetry.itsneufox.xyz` before going on: Caddy can
only get certificates once the names point here. Ports 80 and 443 must be
open in the VPS's firewall.

## 2. Caddy

If Caddy isn't installed yet:

```sh
sudo apt install -y debian-keyring debian-archive-keyring apt-transport-https curl
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo apt update && sudo apt install -y caddy
```

If something else (nginx, Apache) already serves itsneufox.xyz on ports 80
and 443, either move that site into Caddy too, or proxy the two new names from
the existing server to `127.0.0.1:8787` instead of using Caddy, with the same
body limits (1 KB for pings, 256 KB for reports) and no access log.

Add the two blocks from this folder's `Caddyfile` to `/etc/caddy/Caddyfile`,
then:

```sh
sudo systemctl reload caddy
```

## 3. The service

```sh
sudo apt install -y python3 binutils llvm   # binutils/llvm: symbol names for crash stacks
sudo useradd --system --no-create-home --shell /usr/sbin/nologin haumea-server
sudo mkdir -p /opt/haumea-server /etc/haumea-server
sudo cp haumea_server.py symbols.py symbolize.py privacy.html /opt/haumea-server/
sudo cp haumea-server.service /etc/systemd/system/
```

## 4. Settings, the password and Discord

Create `/etc/haumea-server/env`, readable only by root and the service:

```sh
sudo install -m 640 -o root -g haumea-server /dev/null /etc/haumea-server/env
sudoedit /etc/haumea-server/env
```

with:

```sh
# The dashboard's login (https://reports.itsneufox.xyz/dashboard)
HAUMEA_DASH_USER=admin
HAUMEA_DASH_PASSWORD=pick-a-long-random-password

# Discord alerts: in your Discord channel, Settings > Integrations >
# Webhooks > New Webhook > Copy Webhook URL, and paste it here.
DISCORD_WEBHOOK_URL=https://discord.com/api/webhooks/...

# Optional (defaults shown)
#HAUMEA_ALERT_SPIKE=10        reports of one crash group in an hour that alert as a spike
#HAUMEA_ALERT_EVERY=60        seconds between alert messages (alerts in between are sent together)
#HAUMEA_DAILY_SUMMARY=0       1: a daily summary at 09:00 UTC
#HAUMEA_PING_DAYS=365         pings are deleted this long after an install's last one
#HAUMEA_REPORT_DAYS=183       reports are deleted after this long
#HAUMEA_RATE_PINGS=30         per IP address per minute (counted in memory only)
#HAUMEA_RATE_REPORTS=10
```

`pwgen -s 32 1` or `openssl rand -base64 24` makes a good password. The
webhook URL is a secret: anyone with it can post to the channel. It lives only
in this file; the service never logs it.

## 5. Start it

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now haumea-server
systemctl status haumea-server
curl -s https://telemetry.itsneufox.xyz/privacy | head -5
```

Then open `https://reports.itsneufox.xyz/dashboard` and log in. Test a ping:

```sh
curl -si https://telemetry.itsneufox.xyz/haumea/telemetry/ping -H 'Content-Type: application/json' \
  -d '{"uid":"00000000000000000000000000000000","version":"1.0.0.1","channel":"dev","os":"linux"}'
```

It should answer `204 No Content`, and the dashboard counts one active
install for today.

## Updating

Copy the new files over those in `/opt/haumea-server/` and
`sudo systemctl restart haumea-server`. The database keeps its data across
updates; the service adds new tables itself.

## Backups

Everything is in `/var/lib/haumea-server/`: `haumea.db` (pings, reports,
crash groups, the "fixed in" marks) and `symbols/` (downloaded symbol files,
which come back by themselves). A consistent copy while it runs:

```sh
sudo sqlite3 /var/lib/haumea-server/haumea.db ".backup '/root/haumea-$(date +%F).db'"
```

Keep copies off the VPS too (e.g. `rsync` them home). Old reports and pings
are deleted by the service itself (see the retention settings), so backups
don't grow without end.

## Symbol names for crash stacks

Crash reports carry offsets, not function names. When a report comes in, the
service fetches that release's `haumea-symbols-<version>.zip` from GitHub once
(into `/var/lib/haumea-server/symbols/`), and then shows names and lines in
the dashboard and links the same crash across versions. Names are only given
to reports from the release's own binaries: the report's build ID must match
the zip's `BUILD_IDS.txt`. Dev and PR builds, local builds and the MinGW engine
in the Namaka companion keep their stacks as offsets. To read a report by hand
(`--any-build` skips the build ID check):

```sh
python3 /opt/haumea-server/symbolize.py report.json haumea-symbols-1.0.0.57.zip
```

## Privacy

The service never stores IP addresses and keeps no access log (Caddy's is
off for both names). `https://telemetry.itsneufox.xyz/privacy` says what is
collected and for how long; keep it in step with the user guide's Privacy
section.
