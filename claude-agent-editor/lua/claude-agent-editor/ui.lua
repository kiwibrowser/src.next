-- UI: floating-window JSON editor for an agent configuration.
-- The buffer is treated as JSON; on :w (BufWriteCmd) we parse it,
-- validate, and persist via storage.

local config = require("claude-agent-editor.config")
local storage = require("claude-agent-editor.storage")

local M = {}

local function notify(msg, level)
  vim.notify("[claude-agent-editor] " .. msg, level or vim.log.levels.INFO)
end

-- Build the help header shown as comments at the top of the buffer.
-- JSON does not support comments, so we strip them before parsing.
local function header_lines()
  return {
    "// Edit this agent's configuration as JSON.",
    "// :w  saves   |   :ClaudeAgentTest <name>  sends a test prompt",
    "// Available models: " .. table.concat(config.models, ", "),
    "",
  }
end

local function strip_comments(lines)
  local out = {}
  for _, line in ipairs(lines) do
    if not line:match("^%s*//") then
      table.insert(out, line)
    end
  end
  return table.concat(out, "\n")
end

local function open_float(bufnr, title)
  local ui = config.options.ui
  local cols = vim.o.columns
  local rows = vim.o.lines
  local width = math.floor(cols * ui.width_ratio)
  local height = math.floor(rows * ui.height_ratio)
  local row = math.floor((rows - height) / 2)
  local col = math.floor((cols - width) / 2)
  vim.api.nvim_open_win(bufnr, true, {
    relative = "editor",
    width = width,
    height = height,
    row = row,
    col = col,
    style = "minimal",
    border = ui.border,
    title = title,
    title_pos = "center",
  })
end

-- Render an agent table into the buffer (header + pretty JSON).
local function render(bufnr, agent)
  local lines = header_lines()
  -- vim.json.encode is compact; expand it with a tiny pretty-printer to keep
  -- the editor experience reasonable without pulling external deps.
  local pretty = M._pretty(agent)
  for _, l in ipairs(vim.split(pretty, "\n", { plain = true })) do
    table.insert(lines, l)
  end
  vim.api.nvim_buf_set_lines(bufnr, 0, -1, false, lines)
  vim.bo[bufnr].modified = false
end

-- Minimal JSON pretty-printer that re-encodes a table with 2-space indent.
-- It avoids depending on a third-party library; for the small agent schema
-- we use this is more than enough.
function M._pretty(value, indent)
  indent = indent or 0
  local pad = string.rep("  ", indent)
  local pad_next = string.rep("  ", indent + 1)
  local t = type(value)
  if t == "nil" then
    return "null"
  elseif t == "boolean" or t == "number" then
    return tostring(value)
  elseif t == "string" then
    return vim.json.encode(value)
  elseif t == "table" then
    -- Distinguish arrays from objects by checking for a 1-indexed sequence.
    local is_array = (#value > 0) or (next(value) == nil and getmetatable(value) == nil)
    if is_array and #value > 0 then
      local parts = {}
      for _, v in ipairs(value) do
        table.insert(parts, pad_next .. M._pretty(v, indent + 1))
      end
      return "[\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "]"
    end
    -- Object: stable key order to keep diffs predictable.
    local keys = {}
    for k in pairs(value) do table.insert(keys, k) end
    table.sort(keys)
    if #keys == 0 then return "{}" end
    local parts = {}
    for _, k in ipairs(keys) do
      table.insert(parts, pad_next .. vim.json.encode(k) .. ": " .. M._pretty(value[k], indent + 1))
    end
    return "{\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "}"
  end
  return "null"
end

local function attach_save(bufnr)
  vim.api.nvim_create_autocmd("BufWriteCmd", {
    buffer = bufnr,
    callback = function()
      local lines = vim.api.nvim_buf_get_lines(bufnr, 0, -1, false)
      local raw = strip_comments(lines)
      local ok, decoded = pcall(vim.json.decode, raw)
      if not ok then
        notify("invalid JSON: " .. tostring(decoded), vim.log.levels.ERROR)
        return
      end
      local ok2, path = storage.write(decoded)
      if not ok2 then
        notify("save failed: " .. tostring(path), vim.log.levels.ERROR)
        return
      end
      vim.bo[bufnr].modified = false
      notify("saved " .. path)
    end,
  })
end

function M.open(agent)
  local bufnr = vim.api.nvim_create_buf(false, true)
  vim.bo[bufnr].filetype = "json"
  vim.bo[bufnr].bufhidden = "wipe"
  vim.bo[bufnr].buftype = "acwrite" -- enables BufWriteCmd handling
  vim.api.nvim_buf_set_name(bufnr, "claude-agent://" .. agent.name)
  render(bufnr, agent)
  open_float(bufnr, " Claude Agent: " .. agent.name .. " ")
  attach_save(bufnr)
end

return M
