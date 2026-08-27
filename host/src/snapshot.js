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
//
// Output format (ego-parity, docs/ego-parity-notes.md — keep in lockstep with
// the kernel composer in chromium/src-overlay/prism/browser/snapshot/):
//   curated lowercase roles (root/anchor/heading/paragraph/text/...), text
//   folded into `text "..."` child lines, StaticText→text, InlineTextBox never
//   rendered, decorative unnamed nodes skipped (children hoisted), annotations
//   `[ref=N, loc=..., url=...]` in that order, links use loc=href:<url>.

const ROLE_MAP = {
  RootWebArea: "root",
  link: "anchor",
  StaticText: "text",
  heading: "heading",
  paragraph: "paragraph",
  list: "list",
  listitem: "item",
  image: "image",
  img: "image",
  button: "button",
  searchbox: "textbox",
  textbox: "textbox",
  checkbox: "checkbox",
  radio: "radio",
  switch: "switch",
  combobox: "combobox",
  listbox: "listbox",
  menuitem: "menuitem",
  tab: "tab",
  option: "option",
  slider: "slider",
  spinbutton: "spinbutton",
};

const ACTIONABLE_ROLES = new Set([
  "button",
  "anchor",
  "textbox",
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

const CONTAINER_ROLES = new Set(["root", "heading", "paragraph", "list", "item"]);

function mapRole(role) {
  if (!role) return "node";
  if (ROLE_MAP[role]) return ROLE_MAP[role];
  return role.toLowerCase().replace(/ /g, "");
}

function axValue(node, key) {
  const entry = node?.[key];
  if (!entry) return "";
  const value = entry.value;
  return typeof value === "string" ? value : value != null ? String(value) : "";
}

function axUrl(node) {
  for (const prop of node?.properties ?? []) {
    if (prop.name === "url") {
      const value = prop.value?.value;
      return typeof value === "string" ? value : "";
    }
  }
  return "";
}

function stableLocator(role, name) {
  if (!name) return null;
  const escaped = name.replace(/\\/g, "\\\\").replace(/"/g, '\\"');
  return `loc=role:${role}[name="${escaped}"]`;
}

function hasActionableDescendant(byId, node) {
  for (const childId of node.childIds ?? []) {
    const child = byId.get(childId);
    if (!child) continue;
    if (ACTIONABLE_ROLES.has(mapRole(axValue(child, "role")))) return true;
    if (hasActionableDescendant(byId, child)) return true;
  }
  return false;
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

  const visitChildren = (node, depth) => {
    let pendingText = "";
    const flush = () => {
      if (!pendingText) return;
      lines.push(`${"  ".repeat(depth)}- text "${pendingText}"`);
      pendingText = "";
    };
    for (const childId of node.childIds ?? []) {
      const child = byId.get(childId);
      if (!child) continue;
      const childRole = axValue(child, "role");
      if (!child.ignored && mapRole(childRole) === "text") {
        const piece = axValue(child, "name") || axValue(child, "value");
        if (piece) {
          pendingText = pendingText ? `${pendingText} ${piece}` : piece;
          continue;
        }
      }
      flush();
      visit(child, depth);
    }
    flush();
  };

  const visit = (node, depth) => {
    if (!node) return;
    if (node.ignored) {
      // Ignored nodes still parent meaningful children in some trees.
      visitChildren(node, depth);
      return;
    }
    const rawRole = axValue(node, "role");
    if (rawRole === "InlineTextBox") return; // folded into the parent text
    const role = mapRole(rawRole);
    if (role === "text") {
      visitChildren(node, depth);
      return;
    }

    const name = axValue(node, "name");
    const value = axValue(node, "value");
    const url = axUrl(node);
    const display = name || value;
    const actionable = ACTIONABLE_ROLES.has(role);
    const container = CONTAINER_ROLES.has(role);

    // Decorative/noise curation: unnamed, non-actionable, non-container nodes
    // are skipped and their children hoisted; containers with interactive
    // descendants keep their line.
    if (!actionable && !container && !display && !hasActionableDescendant(byId, node)) {
      visitChildren(node, depth);
      return;
    }

    let line = `${"  ".repeat(depth)}- ${role}`;
    // Containers and anchors never print inline (folded text children carry
    // it); other actionable names live in loc= only.
    if (display && !container && !actionable) line += ` "${display}"`;

    const annotations = [];
    const backendNodeId = node.backendDOMNodeId;
    if (actionable && typeof backendNodeId === "number") {
      annotations.push(`ref=${backendNodeId}`);
      if (!seenRefs.has(backendNodeId)) {
        seenRefs.add(backendNodeId);
        refs.push({ backendNodeId, role, name });
      }
      if (options.includeStableLocator !== false) {
        if (role === "anchor" && url) {
          annotations.push(`loc=href:${url}`);
        } else {
          const loc = stableLocator(role, name);
          if (loc) annotations.push(loc);
        }
      }
      if (url) annotations.push(`url=${url}`);
    }
    if (annotations.length) line += ` [${annotations.join(", ")}]`;
    lines.push(line);
    visitChildren(node, depth + 1);
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
