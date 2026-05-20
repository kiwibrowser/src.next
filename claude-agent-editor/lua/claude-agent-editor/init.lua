-- Public surface of the plugin. Re-exports the user-facing operations
-- invoked by the commands in plugin/claude-agent-editor.lua.

local config = require("claude-agent-editor.config")
local storage = require("claude-agent-editor.storage")
local ui = require("claude-agent-editor.ui")
local api = require("claude-agent-editor.api")

local M = {}

function M.setup(user_opts)
  config.merge(user_opts)
end

-- Prompt for a name if one wasn't supplied on the command line.
local function ask_name(prompt, default)
  local ok, name = pcall(vim.fn.input, prompt, default or "")
  if not ok then return nil end
  name = vim.trim(name)
  if name == "" then return nil end
  return name
end

function M.new_agent(name)
  name = name or ask_name("New agent name: ")
  if not name then return end
  if storage.read(name) then
    vim.notify("[claude-agent-editor] agent already exists: " .. name, vim.log.levels.WARN)
    return
  end
  local agent = config.new_agent(name)
  ui.open(agent)
end

function M.edit_agent(name)
  name = name or ask_name("Edit agent name: ")
  if not name then return end
  local agent, err = storage.read(name)
  if not agent then
    vim.notify("[claude-agent-editor] not found: " .. tostring(err), vim.log.levels.ERROR)
    return
  end
  ui.open(agent)
end

function M.list_agent_names()
  return storage.list()
end

function M.list_agents()
  local names = storage.list()
  if #names == 0 then
    vim.notify("[claude-agent-editor] no agents saved yet", vim.log.levels.INFO)
    return
  end
  print("Claude agents:")
  for _, n in ipairs(names) do print("  - " .. n) end
end

function M.delete_agent(name)
  name = name or ask_name("Delete agent name: ")
  if not name then return end
  local ok, err = storage.delete(name)
  if not ok then
    vim.notify("[claude-agent-editor] delete failed: " .. tostring(err), vim.log.levels.ERROR)
    return
  end
  vim.notify("[claude-agent-editor] deleted " .. name)
end

function M.test_agent(name)
  name = name or ask_name("Test agent name: ")
  if not name then return end
  local agent, err = storage.read(name)
  if not agent then
    vim.notify("[claude-agent-editor] not found: " .. tostring(err), vim.log.levels.ERROR)
    return
  end
  local ok, prompt = pcall(vim.fn.input, "Prompt: ")
  if not ok or prompt == "" then return end
  vim.notify("[claude-agent-editor] sending to " .. agent.model .. "...")
  api.send(agent, prompt, function(text, send_err)
    if send_err then
      vim.notify("[claude-agent-editor] " .. send_err, vim.log.levels.ERROR)
      return
    end
    -- Show the response in a scratch buffer so multi-line output is readable.
    local buf = vim.api.nvim_create_buf(false, true)
    vim.api.nvim_buf_set_lines(buf, 0, -1, false, vim.split(text or "", "\n", { plain = true }))
    vim.bo[buf].filetype = "markdown"
    vim.bo[buf].bufhidden = "wipe"
    vim.api.nvim_buf_set_name(buf, "claude-agent-response://" .. agent.name)
    vim.cmd("botright split")
    vim.api.nvim_win_set_buf(0, buf)
  end)
end

return M
