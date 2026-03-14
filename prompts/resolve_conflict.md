# Resolve Merge Conflict

You are resolving merge conflicts for branch: {{branch}}

## Conflict Content
```
{{conflict_content}}
```

## Git Conflict Marker Format

The conflict content uses git's standard markers:
- `<<<<<<< HEAD` ... `=======` : Changes from the base branch (current HEAD)
- `=======` ... `>>>>>>> branch-name` : Incoming changes from the feature branch

## Instructions

1. Merge both sides intelligently - combine changes when they don't fundamentally conflict
2. When there's a true logical conflict, prefer the incoming (feature branch) changes
3. Output ONLY the resolved file content - no explanations, no markdown code fences
4. The output must be complete, syntactically correct, and ready to use as-is

Do not ask questions. Output the final merged file immediately.
