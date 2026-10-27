#!/usr/bin/env python3
"""Idempotent ChirpStack v4 provisioning for the SENSE ADR lab (stdlib only).

Talks to the chirpstack-rest-api service (default http://localhost:8090) with
the global API key stored in .api_token next to this script.

Every run first makes sure the application "adr-lab" and the device profile
"ST-WL55-AU915-ADR" exist in the tenant (created with the lab defaults below if
missing). Then, depending on the flags, it creates-or-updates a gateway and/or
an OTAA device. Running it twice with the same arguments changes nothing.

Device-profile flags (--mac-version, --reg-params, --adr-algorithm, ...) are
applied to an *existing* profile only when given on the command line, so a
plain run never reverts edits made in the web UI.

Examples
  # gateway + device in one go
  ./provision.py --gateway-eui 00800000a0001234 --gateway-name conduit-bench \\
                 --dev-eui 0080e115000a1b2c --join-eui 0000000000000000 \\
                 --app-key 2b7e151628aed2a6abf7158809cf4f3c --device-name wl55-1

  # firmware turned out to be LoRaWAN 1.0.3 / RP001-1.0.3 rev A:
  ./provision.py --mac-version 1.0.3 --reg-params A

  # switch ADR algorithm for every device using the profile
  ./provision.py --adr-algorithm lora_lr_fhss      # or: default, lr_fhss

  # remove objects
  ./provision.py --delete --dev-eui 0080e115000a1b2c --gateway-eui 00800000a0001234

  # show what is configured
  ./provision.py --show
"""

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))

DEFAULT_API = os.environ.get("CHIRPSTACK_REST_URL", "http://localhost:8090")
DEFAULT_TOKEN_FILE = os.path.join(HERE, ".api_token")
DEFAULT_APPLICATION = "adr-lab"
DEFAULT_PROFILE = "ST-WL55-AU915-ADR"

# Lab defaults used when the device profile has to be created.
PROFILE_DEFAULTS = {
    "description": "SENSE ADR lab: STM32WL (LoRaMac-node based ST stack), "
                   "AU915 sub-band 2 (channels 8-15 + 65), OTAA, Class A.",
    "region": "AU915",
    "regionConfigId": "au915_1",
    "macVersion": "LORAWAN_1_0_4",
    "regParamsRevision": "RP002_1_0_3",
    "adrAlgorithmId": "default",
    "supportsOtaa": True,
    "supportsClassB": False,
    "supportsClassC": False,
    "uplinkInterval": 15,
    "deviceStatusReqInterval": 1,
    "flushQueueOnActivate": True,
    "payloadCodecRuntime": "NONE",
    "payloadCodecScript": "",
    "autoDetectMeasurements": True,
    "allowRoaming": False,
    "rx1Delay": 0,
    "supportedUplinkDataRates": [],
    "tags": {},
    "measurements": {},
}

MAC_VERSIONS = {
    "1.0.0": "LORAWAN_1_0_0", "1.0.1": "LORAWAN_1_0_1", "1.0.2": "LORAWAN_1_0_2",
    "1.0.3": "LORAWAN_1_0_3", "1.0.4": "LORAWAN_1_0_4", "1.1.0": "LORAWAN_1_1_0",
    "1.1": "LORAWAN_1_1_0",
}
REG_PARAMS = {
    "A": "A", "B": "B",
    "RP002-1.0.0": "RP002_1_0_0", "RP002-1.0.1": "RP002_1_0_1",
    "RP002-1.0.2": "RP002_1_0_2", "RP002-1.0.3": "RP002_1_0_3",
    "RP002-1.0.4": "RP002_1_0_4",
}

UUID_RE = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$", re.I)


class ApiError(Exception):
    def __init__(self, status, message):
        super().__init__("HTTP %s: %s" % (status, message))
        self.status = status


class Client:
    def __init__(self, base, token, verbose=False):
        self.base = base.rstrip("/")
        self.token = token
        self.verbose = verbose

    def call(self, method, path, body=None, query=None):
        url = self.base + path
        if query:
            url += "?" + urllib.parse.urlencode(query)
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(url, data=data, method=method)
        req.add_header("Grpc-Metadata-Authorization", "Bearer " + self.token)
        req.add_header("Accept", "application/json")
        if data is not None:
            req.add_header("Content-Type", "application/json")
        if self.verbose:
            print("  -> %s %s %s" % (method, url, json.dumps(body) if body else ""), file=sys.stderr)
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                raw = resp.read()
        except urllib.error.HTTPError as e:
            raw = e.read()
            try:
                msg = json.loads(raw).get("message", raw.decode(errors="replace"))
            except ValueError:
                msg = raw.decode(errors="replace")
            raise ApiError(e.code, msg) from None
        except urllib.error.URLError as e:
            raise SystemExit("cannot reach ChirpStack REST API at %s (%s) - is "
                             "'docker compose up -d' running?" % (self.base, e.reason))
        return json.loads(raw) if raw.strip() else {}

    def get_or_none(self, path):
        try:
            return self.call("GET", path)
        except ApiError as e:
            if e.status == 404:
                return None
            raise


def hexstr(value, nbytes, what):
    v = re.sub(r"[\s:\-]", "", value or "").lower()
    if v.startswith("0x"):
        v = v[2:]
    if not re.fullmatch(r"[0-9a-f]{%d}" % (2 * nbytes), v):
        raise SystemExit("%s must be %d hex bytes, got %r" % (what, nbytes, value))
    return v


def resolve_tenant(c, tenant):
    if tenant and UUID_RE.match(tenant):
        return tenant
    res = c.call("GET", "/api/tenants", query={"limit": 1000})["result"]
    if not res:
        raise SystemExit("no tenant found")
    if not tenant:
        return res[0]["id"]  # the default "ChirpStack" tenant on a fresh install
    for t in res:
        if t["name"] == tenant:
            return t["id"]
    raise SystemExit("tenant %r not found (have: %s)" % (tenant, ", ".join(t["name"] for t in res)))


def find_by_name(c, path, tenant_id, name, extra_query=None):
    q = {"limit": 1000, "tenantId": tenant_id, "search": name}
    q.update(extra_query or {})
    for item in c.call("GET", path, query=q).get("result", []):
        if item["name"] == name:
            return item["id"]
    return None


def ensure_application(c, tenant_id, name):
    app_id = find_by_name(c, "/api/applications", tenant_id, name)
    if app_id:
        print("application  %-22s exists     id=%s" % (name, app_id))
        return app_id
    app_id = c.call("POST", "/api/applications", {"application": {
        "name": name, "tenantId": tenant_id,
        "description": "SENSE ADR lab (STM32WL end nodes, AU915 sub-band 2)"}})["id"]
    print("application  %-22s created    id=%s" % (name, app_id))
    return app_id


def profile_overrides(args):
    o = {}
    if args.mac_version:
        o["macVersion"] = MAC_VERSIONS[args.mac_version]
    if args.reg_params:
        o["regParamsRevision"] = REG_PARAMS[args.reg_params]
    if args.adr_algorithm:
        o["adrAlgorithmId"] = args.adr_algorithm
    if args.uplink_interval is not None:
        o["uplinkInterval"] = args.uplink_interval
    if args.device_status_req_interval is not None:
        o["deviceStatusReqInterval"] = args.device_status_req_interval
    if args.rx1_delay is not None:
        o["rx1Delay"] = args.rx1_delay
    if args.supported_uplink_drs is not None:
        s = args.supported_uplink_drs.strip()
        o["supportedUplinkDataRates"] = [] if s in ("", "default", "none") else \
            sorted({int(x) for x in s.split(",")})
    if args.region_config_id:
        o["regionConfigId"] = args.region_config_id
    if args.flush_queue_on_activate is not None:
        o["flushQueueOnActivate"] = args.flush_queue_on_activate
    return o


def ensure_device_profile(c, tenant_id, name, overrides):
    dp_id = find_by_name(c, "/api/device-profiles", tenant_id, name, {"tenantOnly": "true"})
    if not dp_id:
        body = dict(PROFILE_DEFAULTS)
        body.update(overrides)
        body.update({"name": name, "tenantId": tenant_id})
        dp_id = c.call("POST", "/api/device-profiles", {"deviceProfile": body})["id"]
        print("device-prof  %-22s created    id=%s" % (name, dp_id))
        return dp_id
    cur = c.call("GET", "/api/device-profiles/" + dp_id)["deviceProfile"]
    changed = {k: v for k, v in overrides.items() if cur.get(k) != v}
    if changed:
        new = dict(cur)
        new.update(changed)
        c.call("PUT", "/api/device-profiles/" + dp_id, {"deviceProfile": new})
        print("device-prof  %-22s updated    id=%s  %s" % (name, dp_id, json.dumps(changed)))
    else:
        print("device-prof  %-22s exists     id=%s" % (name, dp_id))
    return dp_id


def upsert_gateway(c, tenant_id, eui, name, description, stats_interval):
    cur = c.get_or_none("/api/gateways/" + eui)
    want = {"name": name or ("gw-" + eui), "tenantId": tenant_id}
    if description is not None:
        want["description"] = description
    if stats_interval is not None:
        want["statsInterval"] = stats_interval
    if cur is None:
        body = {"gatewayId": eui, "description": "", "statsInterval": 30,
                "location": {}, "tags": {}, "metadata": {}}
        body.update(want)
        c.call("POST", "/api/gateways", {"gateway": body})
        print("gateway      %-22s created    name=%s" % (eui, body["name"]))
        return
    gw = cur["gateway"]
    if name is None:
        want["name"] = gw["name"]
    if gw.get("tenantId") != tenant_id:
        raise SystemExit("gateway %s already exists in another tenant (%s); delete it first"
                         % (eui, gw.get("tenantId")))
    changed = {k: v for k, v in want.items() if gw.get(k) != v}
    if not changed:
        print("gateway      %-22s unchanged  name=%s lastSeen=%s"
              % (eui, gw["name"], cur.get("lastSeenAt") or "never"))
        return
    gw.update(changed)
    c.call("PUT", "/api/gateways/" + eui, {"gateway": gw})
    print("gateway      %-22s updated    %s" % (eui, json.dumps(changed)))


def upsert_device(c, app_id, dp_id, args):
    eui = args.dev_eui
    cur = c.get_or_none("/api/devices/" + eui)
    if cur is None:
        if not (args.join_eui and args.app_key):
            raise SystemExit("new device %s needs --join-eui and --app-key" % eui)
        body = {"devEui": eui, "name": args.device_name or ("wl55-" + eui[-6:]),
                "description": args.device_description or "", "applicationId": app_id,
                "deviceProfileId": dp_id, "joinEui": args.join_eui,
                "skipFcntCheck": bool(args.skip_fcnt_check), "isDisabled": False,
                "tags": {}, "variables": {}}
        c.call("POST", "/api/devices", {"device": body})
        print("device       %-22s created    name=%s" % (eui, body["name"]))
    else:
        dev = cur["device"]
        if dev.get("applicationId") != app_id:
            raise SystemExit("device %s already exists in another application (%s); "
                             "delete it first (--delete --dev-eui %s)" % (eui, dev.get("applicationId"), eui))
        want = {"deviceProfileId": dp_id}
        if args.device_name:
            want["name"] = args.device_name
        if args.device_description is not None:
            want["description"] = args.device_description
        if args.join_eui:
            want["joinEui"] = args.join_eui
        if args.skip_fcnt_check is not None:
            want["skipFcntCheck"] = args.skip_fcnt_check
        changed = {k: v for k, v in want.items() if dev.get(k) != v}
        if changed:
            dev.update(changed)
            dev.pop("devEui", None)
            c.call("PUT", "/api/devices/" + eui, {"device": dev})
            print("device       %-22s updated    %s" % (eui, json.dumps(changed)))
        else:
            print("device       %-22s unchanged  name=%s lastSeen=%s"
                  % (eui, dev["name"], cur.get("lastSeenAt") or "never"))

    if args.app_key:
        # LoRaWAN 1.0.x: ChirpStack v4 stores the 1.0.x AppKey in the *nwkKey*
        # field (appKey is only used by LoRaWAN 1.1 devices).
        nwk_key = args.nwk_key or args.app_key
        app_key = args.app_key if args.nwk_key else "0" * 32
        want_keys = {"nwkKey": nwk_key, "appKey": app_key}
        cur_keys = c.get_or_none("/api/devices/%s/keys" % eui)
        if cur_keys is None:
            c.call("POST", "/api/devices/%s/keys" % eui, {"deviceKeys": dict(want_keys, devEui=eui)})
            print("device-keys  %-22s created" % eui)
        else:
            k = cur_keys["deviceKeys"]
            if k.get("nwkKey") != nwk_key or k.get("appKey") != app_key:
                new = dict(k)
                new.update(want_keys)
                new.pop("devEui", None)
                c.call("PUT", "/api/devices/%s/keys" % eui, {"deviceKeys": new})
                print("device-keys  %-22s updated" % eui)
            else:
                print("device-keys  %-22s unchanged" % eui)


def delete(c, args):
    if args.dev_eui:
        try:
            c.call("DELETE", "/api/devices/" + args.dev_eui)
            print("device       %-22s deleted" % args.dev_eui)
        except ApiError as e:
            if e.status != 404:
                raise
            print("device       %-22s not found" % args.dev_eui)
    if args.gateway_eui:
        try:
            c.call("DELETE", "/api/gateways/" + args.gateway_eui)
            print("gateway      %-22s deleted" % args.gateway_eui)
        except ApiError as e:
            if e.status != 404:
                raise
            print("gateway      %-22s not found" % args.gateway_eui)


def show(c, tenant_id, app_id, dp_id):
    dp = c.call("GET", "/api/device-profiles/" + dp_id)["deviceProfile"]
    keys = ["name", "region", "regionConfigId", "macVersion", "regParamsRevision",
            "adrAlgorithmId", "supportsOtaa", "supportsClassB", "supportsClassC",
            "uplinkInterval", "deviceStatusReqInterval", "flushQueueOnActivate",
            "rx1Delay", "supportedUplinkDataRates"]
    print("\ndevice profile %s:" % dp_id)
    for k in keys:
        print("  %-26s %s" % (k, dp.get(k)))
    gws = c.call("GET", "/api/gateways", query={"limit": 1000, "tenantId": tenant_id})["result"]
    print("\ngateways (%d):" % len(gws))
    for g in gws:
        print("  %s  %-24s state=%s lastSeen=%s" % (g["gatewayId"], g["name"], g.get("state"), g.get("lastSeenAt") or "never"))
    devs = c.call("GET", "/api/devices", query={"limit": 1000, "applicationId": app_id})["result"]
    print("\ndevices in application (%d):" % len(devs))
    for d in devs:
        print("  %s  %-24s lastSeen=%s" % (d["devEui"], d["name"], d.get("lastSeenAt") or "never"))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = p.add_argument_group("connection")
    g.add_argument("--api-url", default=DEFAULT_API, help="REST API base URL (default %(default)s)")
    g.add_argument("--token-file", default=DEFAULT_TOKEN_FILE, help="API key file (default %(default)s)")
    g.add_argument("--tenant", help="tenant name or UUID (default: first tenant, i.e. 'ChirpStack')")
    g.add_argument("--application", default=DEFAULT_APPLICATION, help="application name (default %(default)s)")
    g.add_argument("--device-profile", default=DEFAULT_PROFILE, help="device profile name (default %(default)s)")
    g.add_argument("-v", "--verbose", action="store_true", help="print every REST call")

    g = p.add_argument_group("gateway")
    g.add_argument("--gateway-eui", help="gateway EUI-64 (16 hex chars)")
    g.add_argument("--gateway-name")
    g.add_argument("--gateway-description")
    g.add_argument("--stats-interval", type=int, help="expected gateway stats interval, s (default 30)")

    g = p.add_argument_group("OTAA device")
    g.add_argument("--dev-eui")
    g.add_argument("--join-eui", help="JoinEUI / AppEUI")
    g.add_argument("--app-key", help="LoRaWAN 1.0.x AppKey (stored as ChirpStack nwkKey)")
    g.add_argument("--nwk-key", help="LoRaWAN 1.1 only: NwkKey (then --app-key is the 1.1 AppKey)")
    g.add_argument("--device-name")
    g.add_argument("--device-description")
    g.add_argument("--skip-fcnt-check", action=argparse.BooleanOptionalAction, default=None,
                   help="disable frame-counter validation (debug only)")

    g = p.add_argument_group("device profile (applied only when given)")
    g.add_argument("--mac-version", choices=sorted(MAC_VERSIONS), help="e.g. 1.0.4 (lab default), 1.0.3")
    g.add_argument("--reg-params", choices=sorted(REG_PARAMS), help="e.g. RP002-1.0.3 (lab default), A, B")
    g.add_argument("--adr-algorithm", help="default | lr_fhss | lora_lr_fhss | <plugin id>")
    g.add_argument("--uplink-interval", type=int, help="expected uplink interval, s")
    g.add_argument("--device-status-req-interval", type=int, help="DevStatusReq per day (0 = off)")
    g.add_argument("--rx1-delay", type=int, help="profile RX1 delay override (0 = use region value)")
    g.add_argument("--supported-uplink-drs", help="comma list, e.g. 2,3,4,5 ; 'default' clears it")
    g.add_argument("--region-config-id", help="e.g. au915_1")
    g.add_argument("--flush-queue-on-activate", action=argparse.BooleanOptionalAction, default=None)

    g = p.add_argument_group("actions")
    g.add_argument("--delete", action="store_true", help="delete the given --gateway-eui / --dev-eui")
    g.add_argument("--show", action="store_true", help="print profile, gateways and devices")
    args = p.parse_args()

    if args.gateway_eui:
        args.gateway_eui = hexstr(args.gateway_eui, 8, "--gateway-eui")
    if args.dev_eui:
        args.dev_eui = hexstr(args.dev_eui, 8, "--dev-eui")
    if args.join_eui:
        args.join_eui = hexstr(args.join_eui, 8, "--join-eui")
    if args.app_key:
        args.app_key = hexstr(args.app_key, 16, "--app-key")
    if args.nwk_key:
        args.nwk_key = hexstr(args.nwk_key, 16, "--nwk-key")

    try:
        with open(args.token_file) as f:
            token = f.read().strip()
    except OSError as e:
        raise SystemExit("cannot read API token %s: %s" % (args.token_file, e))

    c = Client(args.api_url, token, args.verbose)
    try:
        if args.delete:
            if not (args.gateway_eui or args.dev_eui):
                raise SystemExit("--delete needs --gateway-eui and/or --dev-eui")
            delete(c, args)
            return
        tenant_id = resolve_tenant(c, args.tenant)
        app_id = ensure_application(c, tenant_id, args.application)
        dp_id = ensure_device_profile(c, tenant_id, args.device_profile, profile_overrides(args))
        if args.gateway_eui:
            upsert_gateway(c, tenant_id, args.gateway_eui, args.gateway_name,
                           args.gateway_description, args.stats_interval)
        if args.dev_eui:
            upsert_device(c, app_id, dp_id, args)
        if args.show:
            show(c, tenant_id, app_id, dp_id)
    except ApiError as e:
        raise SystemExit("ChirpStack API error: %s" % e)


if __name__ == "__main__":
    main()
