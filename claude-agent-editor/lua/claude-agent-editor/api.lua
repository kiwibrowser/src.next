-- Thin wrapper around the Anthropic /v1/messages endpoint.
-- Uses curl via vim.system (Neovim 0.10+) and falls back to jobstart.

local config = require("claude-agent-editor.config")

local M = {}

local function get_api_key()
  local key = os.getenv(config.options.api_key_env)
  if not key or key == "" then
    return nil, "env var " .. config.options.api_key_env .. " is not set"
  end
  return key
end

-- Build the request body for a single user prompt.
local function build_body(agent, prompt)
  local body = {
    model = agent.model,
    max_tokens = agent.max_tokens,
    messages = { { role = "user", content = prompt } },
  }
  if agent.system and agent.system ~= "" then body.system = agent.system end
  if agent.temperature ~= nil then body.temperature = agent.temperature end
  if agent.top_p ~= nil then body.top_p = agent.top_p end
  return vim.json.encode(body)
end

-- Run curl asynchronously. on_done receives (text, err).
function M.send(agent, prompt, on_done)
  local key, err = get_api_key()
  if not key then return on_done(nil, err) end

  local body = build_body(agent, prompt)
  local args = {
    "curl", "-sS", "-X", "POST", config.options.api_url,
    "-H", "content-type: application/json",
    "-H", "x-api-key: " .. key,
    "-H", "anthropic-version: " .. config.options.api_version,
    "--data-binary", "@-",
  }

  local function handle_output(stdout, stderr, code)
    if code ~= 0 then
      return on_done(nil, "curl exited " .. code .. ": " .. (stderr or ""))
    end
    local ok, decoded = pcall(vim.json.decode, stdout or "")
    if not ok then
      return on_done(nil, "invalid JSON response: " .. tostring(decoded))
    end
    if decoded.error then
      return on_done(nil, "api error: " .. (decoded.error.message or vim.inspect(decoded.error)))
    end
    -- Concatenate all text blocks from the assistant's content array.
    local parts = {}
    for _, block in ipairs(decoded.content or {}) do
      if block.type == "text" and block.text then
        table.insert(parts, block.text)
      end
    end
    on_done(table.concat(parts, "\n"))
  end

  if vim.system then
    vim.system(args, { stdin = body, text = true }, function(res)
      vim.schedule(function()
        handle_output(res.stdout, res.stderr, res.code)
      end)
    end)
  else
    -- Fallback for Neovim < 0.10
    local stdout_chunks, stderr_chunks = {}, {}
    local job = vim.fn.jobstart(args, {
      stdout_buffered = true,
      stderr_buffered = true,
      on_stdout = function(_, data) stdout_chunks = data end,
      on_stderr = function(_, data) stderr_chunks = data end,
      on_exit = function(_, code)
        vim.schedule(function()
          handle_output(
            table.concat(stdout_chunks, "\n"),
            table.concat(stderr_chunks, "\n"),
            code
          )
        end)
      end,
    })
    vim.fn.chansend(job, body)
    vim.fn.chanclose(job, "stdin")
  end
end

return M
