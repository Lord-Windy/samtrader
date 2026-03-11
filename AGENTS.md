# AGENTS.md — Samtasker Agent Template

This file instructs AI coding agents on how to use `samtasker` (`st`) to manage tasks within this project.

## Project setup

This git repository is the project. Use the repository name (typically UPPERCASE, alphanumeric only) as the project identifier when creating tasks. Assume `samtasker` is already initialized and configured by the user.

## Creating tickets

### Via CLI flags

```bash
st add --project SAMTRADER --summary "Implement auth middleware" \
  --description "Add JWT validation to all API routes" \
  --labels backend,security
```

### Via JSON (single)

```bash
echo '{"project":"SAMTRADER","summary":"Implement auth middleware","description":"Add JWT validation to all API routes","labels":["backend","security"]}' | st add --json
```

### Via JSON (batch)

```bash
cat <<'EOF' | st add --json
[
  {"project":"SAMTRADER","summary":"Design database schema","labels":["backend"]},
  {"project":"SAMTRADER","summary":"Build API endpoints","labels":["backend"]},
  {"project":"SAMTRADER","summary":"Create frontend components","labels":["frontend"]}
]
EOF
# Output: ["a1b2c3d4", "b2c3d4e5", "c3d4e5f6"] (Array of created task IDs)
```

## Creating tickets with dependencies

Dependencies ensure tickets are worked on in the correct order. A ticket with unresolved dependencies will not appear as ready.

```bash
# Create the first ticket
st add --project SAMTRADER --summary "Design database schema"
# Output: Created task: a1b2c3d4

# Create a dependent ticket
st add --project SAMTRADER --summary "Build API endpoints" --depends-on a1b2c3d4
```

Or via JSON:

```bash
cat <<'EOF' | st add --json
[
  {"project":"SAMTRADER","summary":"Design database schema"},
  {"project":"SAMTRADER","summary":"Build API endpoints","dependencies":["a1b2c3d4"]}
]
EOF
```

You can also add dependencies after creation:

```bash
st depend b7e2d4a1 --on a1b2c3d4
```

## Editing tickets

### Via CLI flags

```bash
st edit a1b2c3d4 --summary "Updated summary"
st edit a1b2c3d4 --description "More detailed description"
st edit a1b2c3d4 --add-labels urgent
st edit a1b2c3d4 --remove-labels low-priority
```

### Via JSON

```bash
echo '{"key":"a1b2c3d4","summary":"Updated summary","labels":["urgent","backend"]}' | st edit --json
```

Note: when editing via JSON, `labels` and `dependencies` replace all existing values (they are not additive).

## Finding tickets ready to work on

```bash
st list --ready --project SAMTRADER
```

*Note: For AI agents, it is highly recommended to append `--json` to `list` and `show` commands for reliable parsing.*
```bash
st list --ready --project SAMTRADER --json
```

This shows only tickets that:
- Belong to the SAMTRADER project
- Have status `ready`
- Have all dependencies resolved (all dependencies are `done` or `cancelled`)

You can combine filters:

```bash
st list --ready --project SAMTRADER --label backend
```

To see full details of a specific ticket:

```bash
st show a1b2c3d4

# Or for reliable parsing:
st show a1b2c3d4 --json
```

## Working on a ticket

When you pick up a ticket, move it to `in-progress`:

```bash
st status a1b2c3d4 in-progress
```

## Completing a ticket

When the implementation is done:

1. **Commit and push the work** to preserve it for review:

```bash
git add <files>
git commit -m "Implement feature X"
git push
```

2. **Set the ticket as done** once pushed:

```bash
st status a1b2c3d4 done
```

Always commit and push before marking a ticket as done. The work must be preserved on the remote for review. Do not mark a ticket done until the code is pushed.

When a ticket is marked done, any dependent tickets that have all their dependencies resolved will automatically be promoted from `backlog` to `ready`.

## Status transitions

```
backlog → ready → in-progress → done
                       ↓
                    blocked → in-progress
                            → ready

Any status → cancelled
```

`done` and `cancelled` are terminal — no further transitions are allowed.

## Quick reference

| Action | Command |
|---|---|
| Create a ticket | `st add --project SAMTRADER --summary "..."` |
| Create with dependency | `st add --project SAMTRADER --summary "..." --depends-on KEY` |
| List ready tickets | `st list --ready --project SAMTRADER [--json]` |
| Show ticket details | `st show KEY [--json]` |
| Start working | `st status KEY in-progress` |
| Mark done | `st status KEY done` |
| Edit summary | `st edit KEY --summary "..."` |
| Add dependency | `st depend KEY --on OTHER_KEY` |
| Remove dependency | `st undepend KEY --on OTHER_KEY` |
| Batch create (JSON) | `cat tasks.json \| st add --json` |
| Export all tickets | `st export --json` |
