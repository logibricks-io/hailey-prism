// Phase 1 snapshot composer: builds the contract snapshot
// ({content, refs}) from a stock Accessibility.getFullAXTree result.
//
// This is the simulation-mode renderer. The kernel renderer (Phase 3,
// Prism.snapshot DevTools domain) replaces it and is the one that has to
// handle cross-process nested iframes; this JS version handles what a single
// target's AX tree can see.
//
// Contract invariants implemented here (docs/binding-contract.md §4):
//   - refs entries are {backendNodeId, role, name} with backendNodeId taken
//     from the AX node's backendDOMNodeId (same numbering CDP uses).
//   - node lines carry [ref=N, loc=...] annotations when available.
//   - maxResultLength truncates content.

const ACTIONABLE_ROLES = new Set([
  "button",
  "link",
  "textbox",
  "searchbox",
  "combobox",
  "listbox",
  "menuitem",
  "tab",
  "checkbox",
  "radio",
  "switch",
  "slider",
  "spinbutton",
  "option",
]);

function axValue(node, key) {
  const entry = node?.[key];
  if (!entry) return "";
  const value = entry.value;
  return typeof value === "string" ? value : value != null ? String(value) : "";
}

function stableLocator(role, name) {
  if (!ACTIONABLE_ROLES.has(role) || !name) return null;
  const escaped = name.replace(/\\/g, "\\\\").replace(/"/g, '\\"');
  return `loc=role:${role}[name="${escaped}"]`;
}

export function composeSnapshot(axNodes, options = {}) {
  const { maxResultLength } = options;
  const byId = new Map();
  let root = null;
  for (const node of axNodes) {
    byId.set(node.nodeId, node);
    if (!node.parentId) root = node;
  }
  const refs = [];
  const seenRefs = new Set();
  const lines = [];

  const visit = (node, depth) => {
    if (!node || node.ignored) {
      // Ignored nodes still parent meaningful children in some trees.
      for (const childId of node?.childIds ?? []) visit(byId.get(childId), depth);
      return;
    }
    const role = axValue(node, "role");
    const name = axValue(node, "name");
    const value = axValue(node, "value");
    const annotations = [];
    const backendNodeId = node.backendDOMNodeId;
    if (typeof backendNodeId === "number") {
      annotations.push(`ref=${backendNodeId}`);
      if (!seenRefs.has(backendNodeId)) {
        seenRefs.add(backendNodeId);
        refs.push({ backendNodeId, role, name });
      }
    }
    if (options.includeStableLocator !== false) {
      const locator = stableLocator(role, name);
      if (locator) annotations.push(locator);
    }
    const suffix = annotations.length ? ` [${annotations.join(", ")}]` : "";
    const label = name ? ` "${name}"` : value ? ` "${value}"` : "";
    lines.push(`${"  ".repeat(depth)}- ${role || "node"}${label}${suffix}`);
    for (const childId of node.childIds ?? []) visit(byId.get(childId), depth + 1);
  };

  if (root) visit(root, 0);

  let content = lines.join("\n");
  if (typeof maxResultLength === "number" && content.length > maxResultLength) {
    content = content.slice(0, Math.max(0, maxResultLength));
  }
  return { content, refs };
}

// Fetches the AX tree for a target and composes the snapshot. Attaches a
// short-lived session per call; cheap enough for the simulation path.
export async function snapshotTarget(cdp, targetId, options = {}) {
  const { sessionId } = await cdp.send("Target.attachToTarget", {
    targetId,
    flatten: true,
  });
  try {
    const { nodes } = await cdp.send(
      "Accessibility.getFullAXTree",
      {},
      sessionId,
    );
    return composeSnapshot(nodes ?? [], options);
  } finally {
    await cdp
      .send("Target.detachFromTarget", { sessionId })
      .catch(() => {});
  }
}
