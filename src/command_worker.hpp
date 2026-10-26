#pragma once
namespace forge {
// Private compiler/game supervision mode. Call before initializing engine worlds
// or task threads. Framed launch input and a separate retirement receipt are not
// part of the public automation/MCP protocol.
int command_worker_stdio() noexcept;
} // namespace forge
