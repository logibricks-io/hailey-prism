// Simulated task spaces for Phase 1 (stock Chromium).
//
// A Space maps to one Target browser context (Target.createBrowserContext) —
// stock Chromium cannot create background/hidden windows, and browser contexts
// give us deterministic per-space target filtering (targetInfo.browserContextId)
// plus atomic teardown (Target.disposeBrowserContext).
//
// Known deviation from product semantics: a browser context has isolated
// storage, so simulated spaces do NOT inherit the profile's login state. The
// kernel-era replacement (ADR-002) maps spaces to hidden window groups inside
// the shared profile, which restores login inheritance. The semantic surface
// (ownership values, method shapes) is identical; see
// docs/binding-contract.md §2.3.
//
// Ownership values follow the contract: "agent" | "user" | "agentDelegatedToUser".

export class SpaceRegistry {
  #spaces = new Map(); // id -> Space
  #nextId = 1;

  // Space: { id, taskId, name, createdBy, ownership, browserContextId|null,
  //          currentTargetId|null, recentTabTitles:string[] }

  list() {
    return [...this.#spaces.values()].map((space) => this.#toWire(space));
  }

  create(name, { createdBy = "agent" } = {}) {
    const id = this.#nextId++;
    const space = {
      id,
      taskId: String(id),
      name: String(name ?? `space-${id}`),
      createdBy,
      ownership: createdBy,
      browserContextId: null,
      currentTargetId: null,
      recentTabTitles: [],
    };
    this.#spaces.set(id, space);
    return this.#toWire(space);
  }

  get(id) {
    return this.#spaces.get(Number(id)) ?? null;
  }

  remove(id) {
    return this.#spaces.delete(Number(id));
  }

  claim(id) {
    const space = this.get(id);
    if (!space) return null;
    space.ownership = "agent";
    return this.#toWire(space);
  }

  handOff(id) {
    const space = this.get(id);
    if (!space) return null;
    if (space.ownership === "agent") space.ownership = "agentDelegatedToUser";
    return this.#toWire(space);
  }

  takeOver(id) {
    const space = this.get(id);
    if (!space) return null;
    if (space.ownership === "agentDelegatedToUser") space.ownership = "agent";
    return this.#toWire(space);
  }

  setBrowserContext(id, browserContextId) {
    const space = this.get(id);
    if (space) space.browserContextId = browserContextId;
  }

  setCurrentTarget(id, targetId) {
    const space = this.get(id);
    if (space) space.currentTargetId = targetId;
  }

  clearTarget(targetId) {
    for (const space of this.#spaces.values()) {
      if (space.currentTargetId === targetId) space.currentTargetId = null;
    }
  }

  ownsTarget(id, targetInfo) {
    const space = this.get(id);
    return (
      !!space &&
      !!targetInfo &&
      targetInfo.browserContextId === space.browserContextId
    );
  }

  #toWire(space) {
    return {
      taskId: String(space.id),
      id: space.id,
      name: space.name,
      createdBy: space.createdBy,
      ownership: space.ownership,
      recentTabTitles: space.recentTabTitles ?? [],
    };
  }
}
