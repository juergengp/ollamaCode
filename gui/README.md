# OllamaCode GUI

Native macOS GUI application for OllamaCode - a Claude Code-inspired AI coding assistant that runs locally with Ollama.

## Features

- **Native SwiftUI Interface** - Modern macOS app with familiar UI patterns
- **Full Agent Support** - Explorer, Coder, Runner, Planner, and General agents
- **Tool Execution** - Execute Bash, Read, Write, Edit, Glob, and Grep operations with visual confirmation
- **Streaming Responses** - See AI responses as they're generated
- **Session Management** - Save, load, and export conversation sessions
- **Shared Configuration** - Uses the same SQLite config as the CLI (`~/.config/ollamacode/config.db`)
- **MCP Integration** - Connect to Model Context Protocol servers
- **Safe Mode** - Command whitelisting and execution confirmation

## Requirements

- macOS 13.0 (Ventura) or later
- Xcode 15.0 or later
- Swift 5.9 or later
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) (for project generation)
- Ollama installed and running

## Building

### 1. Install XcodeGen

```bash
brew install xcodegen
```

### 2. Generate Xcode Project

```bash
cd gui
xcodegen generate
```

This creates `OllamaCode.xcodeproj` from `project.yml`.

### 3. Open in Xcode

```bash
open OllamaCode.xcodeproj
```

### 4. Build and Run

- Select the "OllamaCode" scheme
- Press Cmd+R to build and run
- Or build from command line:

```bash
xcodebuild -project OllamaCode.xcodeproj -scheme OllamaCode -configuration Release build
```

## Project Structure

```
gui/
├── project.yml                 # XcodeGen configuration
├── OllamaCode/
│   ├── App/
│   │   └── OllamaCodeApp.swift # App entry point
│   ├── Views/
│   │   ├── MainView.swift      # Main window with sidebar
│   │   ├── ChatView.swift      # Chat message list and input
│   │   ├── MessageView.swift   # Individual message rendering
│   │   ├── SettingsView.swift  # Preferences window
│   │   └── AgentSelectorView.swift
│   ├── ViewModels/
│   │   └── ChatViewModel.swift # Main chat state management
│   ├── Models/
│   │   ├── Message.swift       # Message data structures
│   │   ├── Agent.swift         # Agent definitions
│   │   ├── ToolCall.swift      # Tool call structures
│   │   ├── ToolResult.swift    # Tool execution results
│   │   └── Session.swift       # Session management
│   ├── Services/
│   │   ├── ConfigService.swift     # SQLite configuration
│   │   ├── OllamaService.swift     # HTTP client for Ollama
│   │   ├── ToolParserService.swift # XML tool call parsing
│   │   ├── ToolExecutorService.swift # Tool execution
│   │   └── SessionService.swift    # Session persistence
│   └── Resources/
│       ├── Info.plist
│       └── OllamaCode.entitlements
└── OllamaCodeTests/
```

## Architecture

### MVVM Pattern
- **Views**: SwiftUI declarative UI components
- **ViewModels**: ObservableObject classes managing state
- **Models**: Data structures (Message, Agent, ToolCall, etc.)
- **Services**: Business logic (HTTP, SQLite, tool execution)

### Shared Configuration
Both CLI and GUI use the same configuration:
- `~/.config/ollamacode/config.db` - Settings and history
- `~/.config/ollamacode/mcp_servers.json` - MCP servers

This means you can switch between CLI and GUI seamlessly.

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Cmd+N | New conversation |
| Cmd+K | Clear conversation |
| Cmd+Enter | Send message |
| Cmd+, | Open settings |

## Configuration

### Ollama Host
Default: `http://localhost:11434`

To connect to a remote Ollama instance, change this in Settings > General.

### Models
Available models are automatically discovered from Ollama. Select your preferred model in Settings > Model.

### Safe Mode
When enabled, only whitelisted commands can be executed. Configure allowed commands in Settings > Safety.

## Development

### Adding New Features

1. **New Tool Type**:
   - Add to `ToolType` enum in `Models/ToolCall.swift`
   - Implement execution in `Services/ToolExecutorService.swift`
   - Update parser in `Services/ToolParserService.swift`

2. **New Agent**:
   - Add to `AgentType` enum in `Models/Agent.swift`
   - Define allowed tools and system prompt

3. **New View**:
   - Create SwiftUI view in `Views/`
   - Add to navigation in `MainView.swift`

### Testing

```bash
xcodebuild test -project OllamaCode.xcodeproj -scheme OllamaCode
```

## Dependencies

- **SQLite.swift** - Database access (via Swift Package Manager)

## Troubleshooting

### "Cannot connect to Ollama"
1. Ensure Ollama is running: `ollama serve`
2. Check the host URL in Settings
3. Try accessing `http://localhost:11434/api/tags` in a browser

### "Project generation failed"
1. Ensure XcodeGen is installed: `brew install xcodegen`
2. Run from the `gui/` directory
3. Check `project.yml` for syntax errors

### "Build failed - missing SQLite"
The SQLite.swift dependency should be resolved automatically. If not:
1. Open Xcode
2. File > Packages > Reset Package Caches
3. File > Packages > Resolve Package Versions

## License

Same as the main ollamaCode project.
