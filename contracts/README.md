# Contracts

The current client/server contract is the Qt Socket protocol:

- `socket-protocol.md`: transport and retry rules;
- `schemas/socket.json`: machine-readable envelope and 39 action schemas;
- `examples/socket-charging-flow.json`: complete request sequence;
- `enums.json`: shared v0.4 business enums and units.

`openapi.yaml` and the older dashboard, prediction, telemetry, and HTTP examples are retained only as migration references for the Python implementation. New Qt clients must not use them as runtime contracts.
