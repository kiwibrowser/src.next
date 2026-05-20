-- Persistence layer: each agent is a single JSON file under storage_dir,
-- filename derived from a sanitized version of the agent name.

local config = require("claude-agent-editor.config")

local M = {}

local function ensure_dir()
  local dir = config.options.storage_dir
  vim.fn.mkdir(dir, "p")
  return dir
end

-- Map an agent name to a safe filename.
local function slugify(name)
  local s = name:lower():gsub("[^%w%-_%.]+", "-"):gsub("^%-+", ""):gsub("%-+$", "")
  if s == "" then s = "agent" end
  return s
end

function M.path_for(name)
  return ensure_dir() .. "/" .. slugify(name) .. ".json"
end

function M.list()
  local dir = ensure_dir()
  local names = {}
  local handle = vim.loop.fs_scandir(dir)
  if not handle then return names end
  while true do
    local entry, t = vim.loop.fs_scandir_next(handle)
    if not entry then break end
    if (t == "file" or t == nil) and entry:match("%.json$") then
      local ok, agent = pcall(M.read_path, dir .. "/" .. entry)
      if ok and agent and agent.name then
        table.insert(names, agent.name)
      end
    end
  end
  table.sort(names)
  return names
end

function M.read_path(path)
  local fd, err = io.open(path, "r")
  if not fd then return nil, err end
  local data = fd:read("*a")
  fd:close()
  local ok, decoded = pcall(vim.json.decode, data)
  if not ok then return nil, decoded end
  return decoded
end

function M.read(name)
  return M.read_path(M.path_for(name))
end

function M.write(agent)
  local ok, err = config.validate(agent)
  if not ok then return false, err end
  local path = M.path_for(agent.name)
  local fd, ferr = io.open(path, "w")
  if not fd then return false, ferr end
  -- Pretty-printed JSON keeps diffs readable when users version-control it.
  local encoded = vim.json.encode(agent)
  fd:write(encoded)
  fd:close()
  return true, path
end

function M.delete(name)
  local path = M.path_for(name)
  local ok, err = os.remove(path)
  if not ok then return false, err end
  return true
end

return M
