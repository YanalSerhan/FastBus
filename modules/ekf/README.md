# `modules/ekf` — EKF sensor-fusion component

**Status:** placeholder — implemented in M7.

Separate component with its own version (1.0.0) that talks to the rest of the system only through the public API (`fastbus/api`) and generated messages. `EkfCore` (pure math, no fastbus dependency) + `EkfNode` (bus adapter).

| Folder | Contents |
|---|---|
| `include/`, `src/` | EkfCore + EkfNode |
| `idl/` | `ekf_v1.fbidl`, later `ekf_v2.fbidl` |
| `config/` | noise parameters, sim params (versioned JSON) |
| `tests/` | Jacobian, consistency (NEES/NIS), accuracy and replay tests |

**Building-block contract:** Input = IMU / GPS / wheel-odometry messages; Output = `PoseEstimate`; Setup = noise parameters, gating thresholds, initialization policy.
