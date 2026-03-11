# Implement Ticket: {{ticket.key}}

## Summary
{{ticket.summary}}

## Description
{{ticket.description}}

{% if ticket.labels then %}
## Labels
{% for _, label in ipairs(ticket.labels) do %}
- {{label}}
{% end %}
{% end %}

{% if ticket.notes then %}
## Notes
{{ticket.notes}}
{% end %}

## Your Task

Please implement the changes described above. Follow these guidelines:

1. **Code Style**: Follow the existing patterns and conventions in the codebase
2. **Testing**: Write tests for any new functionality
3. **Scope**: Keep changes focused on this ticket only
4. **Documentation**: Update documentation if you change public APIs

## Context

- Project: {{ticket.project}}
- Ticket Key: {{ticket.key}}

When you have completed the implementation, stop and let me know.
