# config/

Versioned JSON configuration (every file carries a `"version"` key validated on load — see `docs/versions.md`).

| Path | Contents | From |
|---|---|---|
| `qos_profiles.json` | named QoS profiles | M3 |
| `fault_scenarios/*.json` | seeded fault-injection scenarios | M6 |
| `sim/*.json` | EKF sensor-simulation parameters | M7 |
| `bench/*.json` | benchmark suite parameters | M9 |
