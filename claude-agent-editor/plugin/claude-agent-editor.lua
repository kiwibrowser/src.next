-- claude-agent-editor: Neovim plugin entry point.
-- Registers user commands. The actual implementation lives in
-- lua/claude-agent-editor/*. Loading is lazy: requiring the module only
-- happens when a command is invoked.

if vim.g.loaded_claude_agent_editor == 1 then
  return
end
vim.g.loaded_claude_agent_editor = 1

local function load()
  return require("claude-agent-editor")
end

vim.api.nvim_create_user_command("ClaudeAgentNew", function(opts)
  load().new_agent(opts.args ~= "" and opts.args or nil)
end, {
  nargs = "?",
  desc = "Create a new Claude agent configuration",
})

vim.api.nvim_create_user_command("ClaudeAgentEdit", function(opts)
  load().edit_agent(opts.args ~= "" and opts.args or nil)
end, {
  nargs = "?",
  complete = function()
    return load().list_agent_names()
  end,
  desc = "Edit an existing Claude agent by name",
})

vim.api.nvim_create_user_command("ClaudeAgentList", function()
  load().list_agents()
end, { desc = "List all saved Claude agents" })

vim.api.nvim_create_user_command("ClaudeAgentDelete", function(opts)
  load().delete_agent(opts.args ~= "" and opts.args or nil)
end, {
  nargs = "?",
  complete = function()
    return load().list_agent_names()
  end,
  desc = "Delete a saved Claude agent",
})

vim.api.nvim_create_user_command("ClaudeAgentTest", function(opts)
  load().test_agent(opts.args ~= "" and opts.args or nil)
end, {
  nargs = "?",
  complete = function()
    return load().list_agent_names()
  end,
  desc = "Send a quick test prompt to a saved agent",
})
