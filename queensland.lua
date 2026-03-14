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
    local prompt = ql.prompt("implement.md", { ticket = ticket })

    local result = ql.ai.run({
        tool = "opencode",
        cwd = dir,
        prompt = prompt,
    })

    if not result.success then
        error("Implementation failed: " .. result.stderr)
    end

    local status_result = ql.exec(dir, "git", "status", "--porcelain")
    if status_result.success and status_result.stdout ~= "" then
        ql.git.commit(dir, string.format("%s: %s", ticket.id, ticket.summary))
        ql.git.push(dir, branch)
    end

    return {
        status = "success",
        dir = dir,
    }
end

function review_ticket(ticket)
    local dir = context.worktree_path
    local branch = "ticket-" .. ticket.id:lower()
    local prompt = ql.prompt("review.md", { ticket = ticket })

    local result = ql.ai.run({
        tool = "opencode",
        cwd = dir,
        prompt = prompt,
    })

    if not result.success then
        error("Review failed: " .. result.stderr)
    end

    local status_result = ql.exec(dir, "git", "status", "--porcelain")
    if status_result.success and status_result.stdout ~= "" then
        ql.git.commit(dir, string.format("%s: %s", ticket.id, ticket.summary))
        ql.git.push(dir, branch)
    end

    ql.git.queue_merge(ticket.id, branch)

    return {
        status = "success",
        dir = dir,
    }
end

if context and context.action == "process_ticket" then
    return process_ticket(context.ticket)
end

if context and context.action == "review_ticket" then
    return review_ticket(context.ticket)
end

if context and context.action == "merge_all" then
    return ql.git.merge_all()
end

return {
    project = "SAMTRADER",
    tickets = fetch_tickets(),
    concurrency = 4,
}
