# OlEg Features

Complete guide to all features available in OlEg v2.0.2

## Table of Contents

- [Interactive Mode](#interactive-mode)
- [Tool Execution](#tool-execution)
- [MCP Support](#mcp-support)
- [Model Management](#model-management)
- [Safety Features](#safety-features)
- [Configuration](#configuration)

---

## Interactive Mode

OlEg provides a rich interactive terminal experience for conversing with your local AI models.

### Starting Interactive Mode

```bash
# Basic start
oleg

# With specific model
oleg -m llama3.1

# With MCP enabled
oleg --mcp

# With auto-approve for tools
oleg -a
```

### Terminal Interface

```
   ____  _ _                       ____          _
  / __ \| | | __ _ _ __ ___   __ _/ ___|___   __| | ___
 | |  | | | |/ _` | '_ ` _ \ / _` | |   / _ \ / _` |/ _ \
 | |__| | | | (_| | | | | | | (_| | |__| (_) | (_| |  __/
  \____/|_|_|\__,_|_| |_| |_|\__,_|\____\___/ \__,_|\___|

Interactive CLI for Ollama - Version 2.0.2 (C++)
Type '/help' for commands, '/exit' to quit

Current Configuration:
  Model:        llama3.1
  Host:         http://localhost:11434
  Temperature:  0.7
  Max Tokens:   4096
  Safe Mode:    true
  Auto Approve: false
  MCP Enabled:  yes

You> _
```

### Command Menu

Press `/` to access the command menu with arrow key navigation:

```
┌─────────────────────────────────┐
│  Select a command:              │
│                                 │
│  > /help     - Show help        │
│    /models   - List models      │
│    /model    - Select model     │
│    /config   - Show config      │
│    /mcp      - MCP status       │
│    /clear    - Clear screen     │
│    /exit     - Exit             │
└─────────────────────────────────┘
```

---

## Tool Execution

The AI can execute various tools to help accomplish tasks. Each tool execution shows what's happening and requires confirmation (unless auto-approve is enabled).

### Available Tools

#### Bash - Execute Shell Commands

```bash
You> List all Python files in the current directory

🔧 Executing 1 tool(s)...
═══════════════════════════════════════
Tool 1/1: Bash
═══════════════════════════════════════

[Tool: Bash]
Description: List Python files
Command: find . -name "*.py" -type f

Execute Bash? (y/n): y

✓ Executing...
=== Output ===
./src/main.py
./src/utils.py
./tests/test_main.py
==============
```

#### Read - Read File Contents

```bash
You> Show me the contents of config.py

🔧 Executing 1 tool(s)...
[Tool: Read]
File: config.py

=== File Contents ===
import os

class Config:
    DEBUG = os.getenv('DEBUG', False)
    DATABASE_URL = os.getenv('DATABASE_URL')
====================
```

#### Write - Create/Overwrite Files

```bash
You> Create a hello.py file that prints Hello World

🔧 Executing 1 tool(s)...
[Tool: Write]
File: hello.py

File exists. Overwrite? (y/n): y

✓ File written successfully
Lines written: 3
```

#### Edit - Modify Existing Files

```bash
You> Change the DEBUG default to True in config.py

🔧 Executing 1 tool(s)...
[Tool: Edit]
File: config.py

Found 1 occurrence(s)
Apply changes? (y/n): y

✓ File edited successfully
Backup saved: config.py.bak
```

#### Glob - Find Files by Pattern

```bash
You> Find all test files

🔧 Executing 1 tool(s)...
[Tool: Glob]
Pattern: test_*.py
Path: .

=== Matching Files ===
./tests/test_main.py
./tests/test_utils.py
./tests/test_config.py
=====================
```

#### Grep - Search in Files

```bash
You> Find all TODO comments

🔧 Executing 1 tool(s)...
[Tool: Grep]
Pattern: TODO
Path: .
Mode: content

=== Search Results ===
./src/main.py:42: # TODO: Add error handling
./src/utils.py:15: # TODO: Optimize this
./README.md:8: - [ ] TODO: Add documentation
=====================
```

---

## MCP Support

Model Context Protocol (MCP) allows OlEg to connect to external services and tools.

### Enabling MCP

```bash
# Enable on startup
oleg --mcp

# Enable interactively
You> /mcp on
✓ MCP enabled
Connecting to MCP servers...
  [MCP] filesystem: connected (3 tools)
  [MCP] github: connected (15 tools)
```

### MCP Commands

| Command | Description |
|---------|-------------|
| `/mcp` | Show MCP status |
| `/mcp on` | Enable MCP |
| `/mcp off` | Disable MCP |
| `/mcp servers` | List configured servers |
| `/mcp tools` | List available MCP tools |

### MCP Status Display

```bash
You> /mcp

MCP Status:
  Enabled: yes
  Servers:
    - filesystem: connected
    - github: connected
    - brave-search: disconnected (disabled)
  Available Tools: 18
```

### Using MCP Tools

```bash
You> Read the README from my GitHub repo

🔧 Executing 1 tool(s)...
[MCP Tool: github/get_file_contents]
Arguments: {
  "owner": "juergengp",
  "repo": "OlEg",
  "path": "README.md"
}

Execute MCP tool? (y/n): y

✓ MCP tool executed successfully

=== MCP Output ===
# OlEg
Interactive CLI for Ollama...
==================
```

### Configuring MCP Servers

Create `~/.config/oleg/mcp_servers.json`:

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/path/to/dir"],
      "enabled": true,
      "transport": "stdio"
    },
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"],
      "env": {
        "GITHUB_PERSONAL_ACCESS_TOKEN": "ghp_xxxxx"
      },
      "enabled": true,
      "transport": "stdio"
    }
  }
}
```

---

## Model Management

### Listing Available Models

```bash
You> /models

Available Models:
  llama3.1
  llama3.1:70b
  codellama
  qwen2.5:14b
  mistral
```

### Interactive Model Selection

```bash
You> /model

Select a model:

  [1] llama3.1 (current)
  [2] llama3.1:70b
  [3] codellama
  [4] qwen2.5:14b
  [5] mistral

Enter number (or 'q' to cancel): 4

✓ Switched to model: qwen2.5:14b
```

### Quick Model Switch

```bash
You> /use codellama
✓ Switched to model: codellama
```

### Command Line Model Selection

```bash
# Use specific model
oleg -m qwen2.5:14b "Write a Python function"

# Use specific model in interactive mode
oleg -m llama3.1:70b
```

---

## Safety Features

OlEg includes multiple safety mechanisms to protect your system.

### Safe Mode

When enabled (default), only pre-approved commands can be executed:

**Allowed commands in safe mode:**
- `ls`, `cat`, `head`, `tail`
- `grep`, `find`
- `git` (status, diff, log)
- `docker` (ps, images)
- `pwd`, `whoami`, `date`
- `echo`, `which`
- `ps`, `df`, `du`
- `wc`, `sort`, `uniq`, `tree`

```bash
# Command blocked in safe mode
[Tool: Bash]
Command: rm -rf /

✗ Command not allowed in safe mode
```

### Toggling Safe Mode

```bash
# Disable safe mode (use with caution!)
You> /safe off
⚠ Safe mode disabled

# Re-enable safe mode
You> /safe on
✓ Safe mode enabled
```

### Confirmation Prompts

Every tool execution requires user confirmation:

```bash
[Tool: Bash]
Description: Delete temporary files
Command: rm -f /tmp/*.log

Execute Bash? (y/n): _
```

### Auto-Approve Mode

For trusted operations, you can enable auto-approve:

```bash
# Enable auto-approve
You> /auto on
⚠ Auto-approve enabled

# Or via command line
oleg -a
```

---

## Configuration

### Configuration Storage

Settings are stored in SQLite at `~/.config/oleg/config.db`

### Viewing Current Configuration

```bash
You> /config

Current Configuration:
  Model:        llama3.1
  Host:         http://localhost:11434
  Temperature:  0.7
  Max Tokens:   4096
  Safe Mode:    true
  Auto Approve: false
  MCP Enabled:  yes
  Working Dir:  /Users/user/projects
```

### Temperature Setting

Control response randomness (0.0 = deterministic, 2.0 = very creative):

```bash
# Set temperature
You> /temp 0.3
✓ Temperature set to: 0.3

# Or via command line
oleg -t 0.5 "Generate test cases"
```

### Command Line Options

```
Usage: oleg [OPTIONS] [PROMPT]

OPTIONS:
    -m, --model MODEL       Use specific model
    -t, --temperature NUM   Set temperature (0.0-2.0)
    -a, --auto-approve      Auto-approve all tool executions
    --unsafe                Disable safe mode
    --mcp                   Enable MCP servers on startup
    -v, --version           Show version
    -h, --help              Show help
```

---

## Tips & Best Practices

### 1. Choose the Right Model

- **General coding**: `llama3.1` - Good balance
- **Complex tasks**: `llama3.1:70b` - Better reasoning
- **Fast responses**: `codellama:7b` - Quick but less capable
- **Following instructions**: `qwen2.5:14b` - Excellent compliance

### 2. Use Appropriate Temperature

- **Code generation**: 0.1-0.3 (more deterministic)
- **Creative writing**: 0.7-1.0 (more varied)
- **Problem solving**: 0.3-0.5 (balanced)

### 3. Leverage MCP for Extended Capabilities

Connect to external services:
- **GitHub** - Repository management
- **Filesystem** - Extended file access
- **Database** - SQL queries
- **Web search** - Information retrieval

### 4. Keep Safe Mode Enabled

Only disable safe mode when you:
- Fully trust the AI's actions
- Are in a sandboxed environment
- Need to run specific system commands

### 5. Review Tool Actions

Always review what the AI wants to execute before confirming, especially for:
- File modifications
- System commands
- External API calls
