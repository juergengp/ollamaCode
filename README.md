<p align="center">
  <img src="docs/assets/logo.svg" alt="ollamaCode Logo" width="120">
</p>

<h1 align="center">ollamaCode</h1>

<p align="center">
  <strong>Run AI coding assistants locally with Ollama - Claude Code experience, zero cloud dependency</strong>
</p>

<p align="center">
  <a href="#features">Features</a> •
  <a href="#quick-start">Quick Start</a> •
  <a href="#mcp-support">MCP Support</a> •
  <a href="#installation">Installation</a> •
  <a href="#documentation">Documentation</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-2.0.3-blue.svg" alt="Version">
  <img src="https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg" alt="Platform">
  <img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License">
  <img src="https://img.shields.io/badge/Ollama-compatible-orange.svg" alt="Ollama">
  <img src="https://img.shields.io/badge/MCP-supported-purple.svg" alt="MCP">
</p>

---

## What's New in v2.0.3

- **Specialized Agent System** - Automatic task analysis with agent suggestions (Explorer, Coder, Runner, Planner)
- **Interactive Selection Menus** - Arrow-key navigation for agent and tool selection (like Claude Code)
- **Diff Display for Code Changes** - Shows lines added/removed when editing files
- **Fixed Tool Parsing** - Improved XML parsing for reliable tool execution with all Ollama models

---

## What is ollamaCode?

**ollamaCode** brings the power of AI coding assistants to your local machine. It's like having Claude Code or GitHub Copilot, but running entirely on your hardware with your choice of open-source LLMs via [Ollama](https://ollama.ai).

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│    ____  _ _                       ____          _              │
│   / __ \| | | __ _ _ __ ___   __ _/ ___|___   __| | ___         │
│  | |  | | | |/ _` | '_ ` _ \ / _` | |   / _ \ / _` |/ _ \        │
│  | |__| | | | (_| | | | | | | (_| | |__| (_) | (_| |  __/        │
│   \____/|_|_|\__,_|_| |_| |_|\__,_|\____\___/ \__,_|\___|        │
│                                                                 │
│  Interactive CLI for Ollama - Version 2.0.3 (C++)               │
│  Type '/help' for commands, '/exit' to quit                     │
│                                                                 │
│  Current Configuration:                                         │
│    Model:        llama3.1                                       │
│    Host:         http://localhost:11434                         │
│    Temperature:  0.7                                            │
│    MCP Enabled:  yes                                            │
│                                                                 │
│  You> Find all TODO comments in my project                      │
│                                                                 │
│  🔧 Executing 1 tool(s)...                                      │
│  [Tool: Grep]                                                   │
│  Pattern: TODO                                                  │
│  Path: .                                                        │
│                                                                 │
│  === Search Results ===                                         │
│  src/main.cpp:42: // TODO: Add error handling                   │
│  src/utils.cpp:15: // TODO: Optimize this function              │
│  =====================                                          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Features

### Core Capabilities

| Feature | Description |
|---------|-------------|
| **Local AI** | Run powerful LLMs locally - no API keys, no cloud, complete privacy |
| **Tool Execution** | AI can read files, run commands, search code, and edit files |
| **Specialized Agents** | Explorer, Coder, Runner, Planner agents with focused capabilities |
| **MCP Protocol** | Connect to external services via Model Context Protocol |
| **Interactive CLI** | Beautiful terminal interface with syntax highlighting |
| **Multi-Model** | Switch between models on the fly (`/model`) |
| **Safe Mode** | Command allowlists and confirmation prompts |

### Built-in Tools

The AI assistant has access to these tools to help with your tasks:

```
┌──────────┬────────────────────────────────────────────────────┐
│ Tool     │ Description                                        │
├──────────┼────────────────────────────────────────────────────┤
│ Bash     │ Execute shell commands with safety controls        │
│ Read     │ Read file contents                                 │
│ Write    │ Create or overwrite files                          │
│ Edit     │ Make targeted edits to existing files              │
│ Glob     │ Find files by pattern (e.g., **/*.py)              │
│ Grep     │ Search for text/patterns in files                  │
└──────────┴────────────────────────────────────────────────────┘
```

### Specialized Agents

ollamaCode includes specialized agents that focus on specific tasks with curated tool access:

```
┌────────────┬────────────────────────────────────┬─────────────────────────┐
│ Agent      │ Description                        │ Available Tools         │
├────────────┼────────────────────────────────────┼─────────────────────────┤
│ 🔍 Explorer │ Read-only codebase exploration     │ Glob, Grep, Read        │
│ 💻 Coder    │ Write and modify code              │ Read, Write, Edit, Glob │
│ ▶️ Runner   │ Execute commands and tests         │ Bash, Read              │
│ 📋 Planner  │ Plan tasks without executing       │ Glob, Grep, Read        │
│ 🤖 General  │ Full assistant (default)           │ All tools               │
└────────────┴────────────────────────────────────┴─────────────────────────┘
```

When you enter a prompt, ollamaCode analyzes your task and suggests the most appropriate agent:

```
You> Find all TODO comments in the project

Select an approach:

 > 🔍 explorer - Read-only exploration of codebase
   🤖 Use general agent (all tools)
   ✏️  Enter custom instruction

(Use arrow keys to select, Enter to confirm, Esc to cancel)
```

### MCP (Model Context Protocol) Support

Connect your local Ollama models to external tools and services:

```
┌─────────────────┐     MCP Protocol     ┌──────────────┐
│   ollamaCode    │ ◄──────────────────► │  MCP Servers │
│  (Ollama LLM)   │     (JSON-RPC)       │  - GitHub    │
└─────────────────┘                      │  - Filesystem│
                                         │  - Database  │
                                         │  - Web Search│
                                         └──────────────┘
```

**Available MCP Servers:**
- **filesystem** - Access local files and directories
- **github** - Interact with GitHub repositories
- **brave-search** - Web search capabilities
- **sqlite** - Query SQLite databases
- **puppeteer** - Browser automation
- And many more from the [MCP ecosystem](https://github.com/modelcontextprotocol/servers)

## Quick Start

### 1. Install Ollama

```bash
# macOS / Linux
curl -fsSL https://ollama.ai/install.sh | sh

# Start Ollama
ollama serve
```

### 2. Pull a Model

```bash
# Recommended models for tool use
ollama pull llama3.1        # Good balance of speed and capability
ollama pull qwen2.5:14b     # Excellent at following instructions
ollama pull qwen3:4b        # Fast and lightweight
ollama pull codellama       # Optimized for coding tasks
```

### 3. Install ollamaCode

```bash
# Clone the repository
git clone https://github.com/juergengp/ollamaCode.git
cd ollamaCode

# Option A: Use pre-built binary (macOS)
cp bin/ollamacode /usr/local/bin/

# Option B: Build from source
cd cpp
mkdir build && cd build
cmake ..
make
cp ollamacode /usr/local/bin/
```

### 4. Run!

```bash
# Interactive mode
ollamacode

# Single prompt
ollamacode "Explain this codebase structure"

# With MCP enabled
ollamacode --mcp
```

## Usage Examples

### Code Analysis

```bash
You> Analyze the main.cpp file and suggest improvements

🔧 Executing 1 tool(s)...
[Tool: Read]
File: main.cpp

✓ Success

I've analyzed main.cpp. Here are my suggestions:

1. **Error Handling**: Line 42 lacks proper error handling...
2. **Memory Management**: Consider using smart pointers...
3. **Code Organization**: The function on line 78 is too long...
```

### File Operations

```bash
You> Create a Python script that reads JSON files

🔧 Executing 1 tool(s)...
[Tool: Write]
File: read_json.py

✓ File written successfully
Lines written: 25

I've created read_json.py with the following features:
- Reads JSON files from command line arguments
- Pretty prints the content
- Handles errors gracefully
```

### Search and Navigate

```bash
You> Find all files that import the Config class

🔧 Executing 1 tool(s)...
[Tool: Grep]
Pattern: import.*Config|from.*import.*Config
Mode: files_with_matches

=== Search Results ===
src/main.py
src/utils/settings.py
tests/test_config.py
=====================

Found 3 files that import Config.
```

### MCP Integration

```bash
# Enable MCP and connect to GitHub
You> /mcp on
✓ MCP enabled
Connecting to MCP servers...
  [MCP] github: connected (15 tools)

You> List my recent GitHub issues

🔧 Executing 1 tool(s)...
[MCP Tool: github/list_issues]

Found 5 open issues:
1. #42 - Add support for streaming responses
2. #38 - Improve error messages
...
```

## Interactive Commands

| Command | Description |
|---------|-------------|
| `/help` | Show available commands |
| `/models` | List available Ollama models |
| `/model` | Interactive model selector |
| `/use MODEL` | Switch to a specific model |
| `/temp NUM` | Set temperature (0.0-2.0) |
| `/safe on/off` | Toggle safe mode |
| `/auto on/off` | Toggle auto-approve for tools |
| `/mcp` | Show MCP status |
| `/mcp on/off` | Enable/disable MCP |
| `/mcp tools` | List available MCP tools |
| `/config` | Show current configuration |
| `/clear` | Clear the screen |
| `/exit` | Exit ollamaCode |

### Agent Commands

| Command | Description |
|---------|-------------|
| `/agent` | Show current agent status |
| `/agent on/off` | Enable/disable agent selection mode |
| `/explore` | Switch to Explorer agent (read-only) |
| `/code` | Switch to Coder agent (code changes) |
| `/run` | Switch to Runner agent (commands) |
| `/plan` | Switch to Planner agent (planning) |
| `/general` | Switch to General agent (all tools) |

## Configuration

### Main Configuration

Settings are stored in `~/.config/ollamacode/config.db` (SQLite) and include:

- Model selection
- Temperature
- Max tokens
- Safe mode settings
- Auto-approve settings
- MCP enabled state

### MCP Server Configuration

Configure MCP servers in `~/.config/ollamacode/mcp_servers.json`:

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user/projects"],
      "enabled": true,
      "transport": "stdio"
    },
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"],
      "env": {
        "GITHUB_PERSONAL_ACCESS_TOKEN": "ghp_xxxxxxxxxxxx"
      },
      "enabled": true,
      "transport": "stdio"
    }
  }
}
```

See [docs/MCP_SETUP.md](docs/MCP_SETUP.md) for detailed MCP configuration.

## Recommended Models

| Model | Size | Best For | Tool Use |
|-------|------|----------|----------|
| `llama3.1` | 8B | General coding tasks | Excellent |
| `llama3.1:70b` | 70B | Complex reasoning | Excellent |
| `qwen2.5:14b` | 14B | Instruction following | Excellent |
| `qwen3:4b` | 4B | Fast responses, lightweight | Good |
| `codellama` | 7B | Code generation | Good |
| `mistral-nemo` | 12B | Balanced performance | Good |
| `deepseek-coder` | 6.7B | Code-specific tasks | Good |

## Installation

### Pre-built Binary (macOS)

```bash
git clone https://github.com/juergengp/ollamaCode.git
cd ollamaCode
sudo cp bin/ollamacode /usr/local/bin/
```

### Build from Source

**Requirements:**
- CMake 3.15+
- C++17 compiler (clang or gcc)
- libcurl
- SQLite3

**macOS:**
```bash
# Install dependencies
brew install cmake curl sqlite3

# Build
cd ollamaCode/cpp
mkdir build && cd build
cmake ..
make
sudo cp ollamacode /usr/local/bin/
```

**Linux (Debian/Ubuntu):**
```bash
# Install dependencies
sudo apt install cmake build-essential libcurl4-openssl-dev libsqlite3-dev

# Build
cd ollamaCode/cpp
mkdir build && cd build
cmake ..
make
sudo cp ollamacode /usr/local/bin/
```

**Linux (Fedora/RHEL):**
```bash
# Install dependencies
sudo dnf install cmake gcc-c++ libcurl-devel sqlite-devel

# Build
cd ollamaCode/cpp
mkdir build && cd build
cmake ..
make
sudo cp ollamacode /usr/local/bin/
```

## Documentation

| Document | Description |
|----------|-------------|
| [MCP_SETUP.md](docs/MCP_SETUP.md) | Complete MCP configuration guide |
| [MACOS_QUICKSTART.md](MACOS_QUICKSTART.md) | Quick start guide for macOS |
| [MACOS_SETUP.md](MACOS_SETUP.md) | Detailed macOS setup |
| [VERSIONS.md](VERSIONS.md) | Comparison of Bash vs C++ versions |

## Architecture

```
ollamaCode/
├── bin/                    # Pre-built binaries
│   └── ollamacode         # macOS binary
├── cpp/                    # C++ source code
│   ├── include/           # Header files
│   │   ├── agent.h        # Agent system
│   │   ├── cli.h
│   │   ├── config.h
│   │   ├── mcp_client.h   # MCP client
│   │   ├── ollama_client.h
│   │   ├── task_suggester.h # Task analysis
│   │   ├── tool_executor.h
│   │   └── tool_parser.h
│   ├── src/               # Implementation
│   │   ├── main.cpp
│   │   ├── agent.cpp      # Agent definitions
│   │   ├── cli.cpp
│   │   ├── mcp_client.cpp
│   │   ├── task_suggester.cpp
│   │   └── ...
│   └── CMakeLists.txt
├── docs/                   # Documentation
│   └── MCP_SETUP.md
├── examples/              # Example configurations
│   └── mcp_servers.json
└── lib/                   # Bash version (legacy)
```

## Why ollamaCode?

| Feature | ollamaCode | Claude Code | GitHub Copilot |
|---------|------------|-------------|----------------|
| **Privacy** | 100% Local | Cloud | Cloud |
| **Cost** | Free | Subscription | Subscription |
| **Models** | Any Ollama model | Claude only | GPT only |
| **Offline** | Yes | No | No |
| **MCP Support** | Yes | Yes | No |
| **Open Source** | Yes | No | No |
| **Customizable** | Fully | Limited | Limited |

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- [Ollama](https://ollama.ai) - For making local LLMs accessible
- [Anthropic](https://anthropic.com) - For the MCP protocol specification
- [nlohmann/json](https://github.com/nlohmann/json) - JSON library for C++

---

<p align="center">
  Made with ❤️ by <a href="https://core.at">Core.at</a>
</p>

<p align="center">
  <a href="https://github.com/juergengp/ollamaCode/issues">Report Bug</a> •
  <a href="https://github.com/juergengp/ollamaCode/issues">Request Feature</a>
</p>
