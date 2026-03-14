# Review Ticket: {{ticket.id}}

## Summary
{{ticket.summary}}

## Description
{{ticket.description}}

## Your Task

Review the implementation that was just completed for this ticket and **fix any issues you find**.

1. **Verify Implementation**: Check that all requirements from the description are met
2. **Code Quality**: Ensure the code follows project conventions and best practices
3. **Tests**: Verify that tests exist and pass
4. **Edge Cases**: Look for potential issues or edge cases that may have been missed

## Fixing Issues

When you find issues, categorize and fix them:

### Minor Issues (fix immediately)
- Typos, small bugs, missing edge cases
- Code style violations
- Missing error handling
- Minor test gaps
- Documentation tweaks

**Action**: Fix these now, commit with message referencing the ticket.

### Major Issues (stop and report)
- Architectural problems requiring significant refactoring
- Security vulnerabilities
- Breaking changes to existing functionality
- Missing critical features from the ticket requirements
- Performance issues affecting production

**Action**: Do NOT fix. Report back with details and ask for guidance.

## Workflow

1. Review the code against the ticket requirements
2. Fix all minor issues found
3. If major issues found, stop and report them
4. Run lint/typecheck/tests to verify fixes
5. Commit and push changes if any fixes were made
6. Confirm the implementation is ready or list remaining major issues
