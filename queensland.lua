local ql = require("queensland")

ql.ai.register("opencode", {
    bin = "opencode",
    default_args = { "run" },
    timeout = 1200,
})

ql.ai.set_merge_resolver("opencode", "resolve_conflict.md")

local function fetch_tickets()
    local result = ql.exec(".", "st", "list", "--ready", "--project", "SAMTRADER", "--json")

    if not result.success then
        error("Failed to fetch tickets: " .. result.stderr)
    end

    local raw = ql.json.decode(result.stdout)
    local tickets = {}
    for _, t in ipairs(raw) do
        table.insert(tickets, {
            id = t.key,
            key = t.key,
            summary = t.summary,
            description = t.description,
            labels = t.labels,
            status = t.status,
            project = t.project,
        })
    end
    return tickets
end

function process_ticket(ticket)
    local dir = context.worktree_path
    local branch = "ticket-" .. ticket.id:lower()

    -- Phase 1: Implement
    local impl_prompt = ql.prompt("implement.md", { ticket = ticket })

    local impl_result = ql.ai.run({
        tool = "opencode",
        cwd = dir,
        prompt = impl_prompt,
    })

    if not impl_result.success then
        error("Implementation failed: " .. impl_result.stderr)
    end

    local status_result = ql.exec(dir, "git", "status", "--porcelain")
    if status_result.success and status_result.stdout ~= "" then
        ql.git.commit(dir, string.format("%s: %s", ticket.id, ticket.summary))
        ql.git.push(dir, branch)
    end

    -- Phase 2: Review
    local review_prompt = ql.prompt("review.md", { ticket = ticket })

    local review_result = ql.ai.run({
        tool = "opencode",
        cwd = dir,
        prompt = review_prompt,
    })

    if not review_result.success then
        error("Review failed: " .. review_result.stderr)
    end

    status_result = ql.exec(dir, "git", "status", "--porcelain")
    if status_result.success and status_result.stdout ~= "" then
        ql.git.commit(dir, string.format("%s: review %s", ticket.id, ticket.summary))
        ql.git.push(dir, branch)
    end

    -- Queue for merge
    ql.git.queue_merge(ticket.id, branch)

    return {
        status = "success",
        dir = dir,
    }
end

if context and context.action == "process_ticket" then
    return process_ticket(context.ticket)
end

return {
    project = "SAMTRADER",
    tickets = fetch_tickets(),
    concurrency = 4,
}
