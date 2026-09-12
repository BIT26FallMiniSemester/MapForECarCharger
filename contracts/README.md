# Contracts

The current client/server contract is the Qt Socket protocol:

- `socket-protocol.md`: transport and retry rules;
- `schemas/socket.json`: machine-readable envelope and 41 action schemas;
- `examples/socket-charging-flow.json`: complete request sequence;
- `enums.json`: shared v0.4 business enums and units.

The retired HTTP/OpenAPI contract has been removed. Both Qt clients use only this Socket contract.
