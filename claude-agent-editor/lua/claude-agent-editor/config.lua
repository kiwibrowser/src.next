-- Default configuration and per-agent schema validation.

local M = {}

-- Available models. Update as Anthropic releases new ones.
M.models = {
  "claude-opus-4-5",
  "claude-sonnet-4-5",
  "claude-haiku-4-5",
  "claude-opus-4",
  "claude-sonnet-4",
}

-- Plugin-level defaults. Overridable through require("claude-agent-editor").setup{...}
M.defaults = {
  -- Where to store agent JSON files.
  storage_dir = vim.fn.stdpath("data") .. "/claude-agent-editor/agents",
  -- Env var that holds the API key. Read on demand, never persisted to disk.
  api_key_env = "ANTHROPIC_API_KEY",
  -- API endpoint and version header.
  api_url = "https://api.anthropic.com/v1/messages",
  api_version = "2023-06-01",
  -- Defaults applied to brand-new agents.
  agent_defaults = {
    model = "claude-sonnet-4-5",
    temperature = 1.0,
    max_tokens = 1024,
    top_p = nil, -- optional
    system = "You are a helpful assistant.",
  },
  -- UI: floating window proportions.
  ui = {
    width_ratio = 0.7,
    height_ratio = 0.7,
    border = "rounded",
  },
}

M.options = vim.deepcopy(M.defaults)

function M.merge(user)
  if user then
    M.options = vim.tbl_deep_extend("force", M.options, user)
  end
end

-- Build a fresh agent table from defaults, with the provided name.
function M.new_agent(name)
  local agent = vim.deepcopy(M.options.agent_defaults)
  agent.name = name
  return agent
end

-- Validate an agent table parsed from the buffer. Returns ok, err.
function M.validate(agent)
  if type(agent) ~= "table" then
    return false, "agent must be a JSON object"
  end
  if type(agent.name) ~= "string" or agent.name == "" then
    return false, "field 'name' is required and must be a non-empty string"
  end
  if type(agent.model) ~= "string" or agent.model == "" then
    return false, "field 'model' is required"
  end
  if type(agent.max_tokens) ~= "number" or agent.max_tokens < 1 then
    return false, "field 'max_tokens' must be a positive number"
  end
  if agent.temperature ~= nil then
    if type(agent.temperature) ~= "number" or agent.temperature < 0 or agent.temperature > 2 then
      return false, "field 'temperature' must be a number in [0, 2]"
    end
  end
  if agent.top_p ~= nil then
    if type(agent.top_p) ~= "number" or agent.top_p < 0 or agent.top_p > 1 then
      return false, "field 'top_p' must be a number in [0, 1]"
    end
  end
  if agent.system ~= nil and type(agent.system) ~= "string" then
    return false, "field 'system' must be a string if provided"
  end
  return true
end

return M
