# Contributing to Lintel

Thanks for your interest in contributing. Lintel is currently in beta, so the API and internals are still in the works, so please keep that in mind before starting any large piece of work.

---

## Bug Reports

Open an issue and include:

- A minimal reproduction — the smaller the better
- What you expected to happen and what actually happened
- Your compiler version and Windows version

If the bug involves a crash, a stack trace is helpful.

---

## What's In Scope

Good candidates for contributions:

- Bug fixes with clear reproductions
- Documentation improvements
- New node types that fit naturally alongside `TextNode`, `GraphNode`, `ImageNode`
- DSL improvements that don't break existing `.ltl` files

Out of scope for now:

- Cross-platform support — Lintel is Windows-only by design at this stage
- Rendering backend changes
- Breaking API changes unless discussed and agreed on first

---

## Questions

For general questions about usage, open a discussion rather than an issue. Issues are tracked as actionable work items.
