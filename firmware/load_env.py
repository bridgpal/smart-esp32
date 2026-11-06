# PlatformIO pre-build script: reads ../.env (gitignored) and passes its values to the
# firmware as compile-time defaults. Runtime `config set` values in NVS still win.
import os

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

KEYS = {
    "WIFI_SSID": "WIFI_SSID",
    "WIFI_PASS": "WIFI_PASS",
    "API_BASE": "API_BASE",
    "DEVICE_TOKEN": "API_TOKEN",  # same value the Netlify site checks
}


def parse(path):
    values = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, val = line.split("=", 1)
            val = val.strip()
            if len(val) >= 2 and val[0] == val[-1] and val[0] in "\"'":
                val = val[1:-1]
            values[key.strip()] = val
    return values


env_path = os.path.join(env.subst("$PROJECT_DIR"), "..", ".env")  # noqa: F821
if os.path.exists(env_path):
    values = parse(env_path)
    defines = [(macro, env.StringifyMacro(values[key])) for key, macro in KEYS.items() if values.get(key)]  # noqa: F821
    env.Append(CPPDEFINES=defines)  # noqa: F821
    print(f".env: baked in {', '.join(m for m, _ in defines) or 'nothing (all blank)'}")
else:
    print(".env: not found, firmware will rely on `config set` values only")
