# MCP (Model Context Protocol) Setup Guide

OlEg supports MCP (Model Context Protocol), allowing you to connect to external MCP servers and use their tools with your local Ollama models.

## What is MCP?

MCP (Model Context Protocol) is an open protocol that standardizes how AI applications connect to external data sources and tools. It allows AI assistants to:

- Access file systems
- Query databases
- Search the web
- Interact with APIs
- And much more...

## Prerequisites

1. **Node.js 18+** - Required for most MCP servers
   ```bash
   # macOS with Homebrew
   brew install node

   # Or download from nodejs.org
   ```

2. **Python 3.10+** (optional) - For Python-based MCP servers
   ```bash
   # macOS with Homebrew
   brew install python

   # Install uv for Python package management
   pip install uv
   ```

3. **Ollama** - Running locally with your preferred model
   ```bash
   # Start Ollama
   ollama serve

   # Pull a model that supports tool use
   ollama pull llama3.1
   ollama pull qwen2.5:14b
   ```

## Configuration

MCP servers are configured in `~/.config/oleg/mcp_servers.json`:

```json
{
  "mcpServers": {
    "server-name": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-name", "additional-args"],
      "env": {
        "API_KEY": "your-api-key"
      },
      "enabled": true,
      "transport": "stdio"
    }
  }
}
```

### Configuration Fields

| Field | Description |
|-------|-------------|
| `command` | The command to start the server (e.g., `npx`, `uvx`, `python`) |
| `args` | Command line arguments as an array |
| `env` | Environment variables for the server |
| `enabled` | Whether to auto-connect on startup |
| `transport` | Communication protocol (`stdio` or `http`) |
| `url` | URL for HTTP transport (optional) |

## Available MCP Servers

### Official Servers (by Anthropic)

#### Filesystem
Access local files and directories:
```json
{
  "filesystem": {
    "command": "npx",
    "args": ["-y", "@modelcontextprotocol/server-filesystem", "/path/to/allowed/directory"],
    "enabled": true,
    "transport": "stdio"
  }
}
```

#### GitHub
Interact with GitHub repositories:
```json
{
  "github": {
    "command": "npx",
    "args": ["-y", "@modelcontextprotocol/server-github"],
    "env": {
      "GITHUB_PERSONAL_ACCESS_TOKEN": "ghp_your_token_here"
    },
    "enabled": true,
    "transport": "stdio"
  }
}
```

#### Brave Search
Web search capabilities:
```json
{
  "brave-search": {
    "command": "npx",
    "args": ["-y", "@modelcontextprotocol/server-brave-search"],
    "env": {
      "BRAVE_API_KEY": "your-brave-api-key"
    },
    "enabled": true,
    "transport": "stdio"
  }
}
```

#### SQLite
Query SQLite databases:
```json
{
  "sqlite": {
    "command": "npx",
    "args": ["-y", "@modelcontextprotocol/server-sqlite", "--db-path", "/path/to/database.db"],
    "enabled": true,
    "transport": "stdio"
  }
}
```

#### Puppeteer
Browser automation:
```json
{
  "puppeteer": {
    "command": "npx",
    "args": ["-y", "@modelcontextprotocol/server-puppeteer"],
    "enabled": true,
    "transport": "stdio"
  }
}
```

#### Memory
Persistent memory for conversations:
```json
{
  "memory": {
    "command": "npx",
    "args": ["-y", "@modelcontextprotocol/server-memory"],
    "enabled": true,
    "transport": "stdio"
  }
}
```

### Community Servers (Python-based)

#### Fetch (HTTP requests)
```json
{
  "fetch": {
    "command": "uvx",
    "args": ["mcp-server-fetch"],
    "enabled": true,
    "transport": "stdio"
  }
}
```

#### Time
```json
{
  "time": {
    "command": "uvx",
    "args": ["mcp-server-time"],
    "enabled": true,
    "transport": "stdio"
  }
}
```

## Usage

### Enable MCP on Startup
```bash
oleg --mcp
```

### Interactive Commands

| Command | Description |
|---------|-------------|
| `/mcp` | Show MCP status |
| `/mcp on` | Enable MCP and connect to servers |
| `/mcp off` | Disable MCP and disconnect |
| `/mcp servers` | List configured servers |
| `/mcp tools` | List available tools from connected servers |

### Example Session

```
$ oleg --mcp

Connecting to MCP servers...
  [MCP] filesystem: connecting...
  [MCP] filesystem: connected (3 tools)

You> Read the README.md file in my Documents folder

🔧 Executing 1 tool(s)...
[MCP Tool: filesystem/read_file]
Arguments: {"path": "/Users/username/Documents/README.md"}
...
```

## Troubleshooting

### Server won't connect

1. **Check if the server package is installed:**
   ```bash
   npx -y @modelcontextprotocol/server-filesystem --help
   ```

2. **Check permissions for filesystem access**

3. **Verify environment variables are set correctly**

### Tools not appearing

1. Run `/mcp tools` to see what's available
2. Check server logs for errors
3. Ensure the model supports tool calling

### Model doesn't use MCP tools

Not all Ollama models support tool calling well. Try these models:
- `llama3.1` (8B, 70B, 405B)
- `qwen2.5:14b` or larger
- `mistral-nemo`
- `command-r`

## Creating Custom MCP Servers

You can create your own MCP servers using:

- **TypeScript/JavaScript**: Use `@modelcontextprotocol/sdk`
- **Python**: Use `mcp` package

Example Python server:
```python
from mcp.server import Server
from mcp.server.stdio import stdio_server

app = Server("my-server")

@app.tool()
async def my_tool(param: str) -> str:
    """Description of my tool"""
    return f"Result: {param}"

if __name__ == "__main__":
    stdio_server(app)
```

## Resources

- [MCP Specification](https://spec.modelcontextprotocol.io/)
- [Official MCP Servers](https://github.com/modelcontextprotocol/servers)
- [Python SDK](https://github.com/modelcontextprotocol/python-sdk)
- [TypeScript SDK](https://github.com/modelcontextprotocol/typescript-sdk)
