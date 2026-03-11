local ql = require("queensland")

ql.ai.register("opencode", {
    bin = "opencode",
    default_args = {},
    timeout = 1200,
})

ql.ai.set_merge_resolver("opencode", "resolve_conflict.md")

local function fetch_tickets()
    local result = ql.exec(".", "st", "list", "--ready", "--project", "SAMTRADER", "--json")

    if not result.success then
        error("Failed to fetch tickets: " .. result.stderr)
    end

    return ql.json.decode(result.stdout)
end

function process_ticket(ticket)
    local branch = ticket.key:lower():gsub("[^%w%-]", "-")
    local dir = ql.git.worktree_add(branch)
    local prompt = ql.prompt("implement.md", { ticket = ticket })

    local result = ql.ai.run({
        tool = "opencode",
        cwd = dir,
        prompt = prompt,
    })

    if not result.success then
        error("Implementation failed: " .. result.stderr)
    end

    ql.git.commit(dir, string.format("%s: %s", ticket.key, ticket.summary))
    ql.git.push(dir, branch)

    return {
        status = "success",
        branch = branch,
        dir = dir,
    }
end

return {
    project = "SAMTRADER",
    tickets = fetch_tickets(),
    concurrency = 4,
}
