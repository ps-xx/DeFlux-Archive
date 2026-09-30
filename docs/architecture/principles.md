# Architectural principles

Approved constraints for DeFlux. They describe a target that this discontinued snapshot did not reach. The omitted legacy client is not required to satisfy them, and its flows must not be read as platform design.

## 1. Story Mode stays intact for a normal GTA launch

Launching *Grand Theft Auto: San Andreas – The Definitive Edition* through the normal Rockstar/Steam path must leave Story Mode behavior untouched.

DeFlux must not require permanent story-save damage, and it must not become the only way to boot the game.

## 2. Dedicated multiplayer session lifecycle

DeFlux multiplayer uses its own session lifecycle (see [overview](overview.md)).

The legacy Story, skip, suppress, and teleport path from the omitted client is research history only. It is not the architecture of this snapshot.

Frontend skip, FusionFix menu flags, mission mute, airport cutscene skips, and hardcoded Grove Street teleports are prototype techniques. They do not define how a future session enters `WORLD_LOADING` / `SPAWNING` / `PLAYING`.

## 3. Independent rates

Game frame rate, client networking rate, and server simulation tick are independent.

Do not bind replication to `Present`, do not require the server to tick at the game's FPS, and do not treat the client's send rate as the simulation rate.

## 4. Explicit session state machine

Network sessions use an explicit state machine:

`DISCONNECTED` → `CONNECTING` → `HANDSHAKING` → `CONNECTED` → `WORLD_LOADING` → `READY` → `SPAWNING` → `PLAYING`

Transitions are named and testable. There is no implied “we sent a UDP packet, therefore we are playing.”

## 5. No automatic player sessions for unknown endpoints

Unknown or unauthenticated network endpoints must never automatically create player sessions.

A datagram that is not part of an admitted handshake (including spawn requests and player-sync packets) must not allocate a slot as a convenience. The current prototype’s late-join allocation on `SPAWN_REQUEST` / `PLAYER_SYNC` is a research behavior, not the target.

## 6. Game integration behind abstractions

Game integration must eventually expose abstractions (local pawn transform, spawn apply, world ready, input lock) instead of leaking memory implementation into multiplayer systems.

GTA addresses, UE4 `GWorld` chains, and Gameface module layout belong in the Game Adapter, not in Shared protocol types or the Dedicated Server.

## 7. No simulated state presented as real

Never use simulated or fallback player state as if it were verified real game state.

If the adapter cannot read a transform from the game, the Client must treat the sample as invalid or unresolved. It must not publish a probe or last-known placeholder as authoritative world state.

## 8. Extensibility without a GTA-shaped core

The architecture must support future:

- remote player entities
- vehicles
- generic network entities
- a resource system
- scripting
- a server browser
- headless test clients

The core model is “networked entities and sessions,” not “CJ on Grove Street.”

## 9. Do not couple the architecture to localhost

Client, server, launcher, and test tools must address hosts explicitly. `127.0.0.1` may be a local development default in a config or test harness; it is not the network architecture.

Hard-coded loopback reconnect in the prototype is not the platform join path.

## 10. Do not blindly trust clients

Security-sensitive state is server-authoritative. Position, health, weapons, inventory, money, and spawn rights are examples of data a client may *propose* but must not *define*.

The Dedicated Server must not apply client payloads as truth without validation appropriate to that milestone.

## 11. Preserve reverse-engineering research

Existing reverse-engineering research is valuable and must not be discarded without review.

Offsets, UE4 chain notes, FusionFix entry experiments, intro-skip findings, and hook attempts stay in the tree until a milestone explicitly replaces or archives them. Preservation is not permission to keep them in the multiplayer core.

## 12. Milestone-driven and test-driven change

Changes land in reviewed milestones with tests where behavior can be specified without the full game (protocol, session machine, admission, tick independence).

Do not rewrite the prototype “because the docs exist.” Do not migrate folders until a milestone says to. Do not add systems listed in principle 8 until that milestone is scheduled.

## Using these principles

When a later change conflicts with a principle, stop and treat it as an architecture defect, not as a shortcut. When the prototype already conflicts, record the gap and keep the old code until the replacement is ready.
