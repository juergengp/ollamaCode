import SwiftUI

/// Main chat view with message list and input
struct ChatView: View {
    @ObservedObject var viewModel: ChatViewModel
    @FocusState private var isInputFocused: Bool
    @State private var scrollProxy: ScrollViewProxy?

    var body: some View {
        VStack(spacing: 0) {
            // Messages
            messageList

            Divider()

            // Input area
            inputArea
        }
    }

    private var messageList: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(spacing: 16) {
                    if viewModel.messages.isEmpty {
                        emptyStateView
                    } else {
                        ForEach(viewModel.messages) { message in
                            MessageView(message: message)
                                .id(message.id)
                        }
                    }

                    // Loading indicator
                    if viewModel.isLoading {
                        loadingIndicator
                            .id("loading")
                    }
                }
                .padding()
            }
            .onAppear {
                scrollProxy = proxy
            }
            .onChange(of: viewModel.messages.count) { _ in
                scrollToBottom(proxy: proxy)
            }
            .onChange(of: viewModel.isLoading) { _ in
                scrollToBottom(proxy: proxy)
            }
        }
    }

    private var emptyStateView: some View {
        VStack(spacing: 20) {
            Image(systemName: "bubble.left.and.bubble.right")
                .font(.system(size: 60))
                .foregroundColor(.secondary.opacity(0.5))

            Text("Start a conversation")
                .font(.title2)
                .foregroundColor(.secondary)

            Text("Ask me to help with coding tasks, explore files, or run commands.")
                .font(.body)
                .foregroundColor(.secondary.opacity(0.8))
                .multilineTextAlignment(.center)
                .frame(maxWidth: 400)

            // Quick actions
            HStack(spacing: 12) {
                QuickActionButton(text: "Explore codebase", icon: "magnifyingglass") {
                    viewModel.inputText = "Help me explore this codebase"
                }
                QuickActionButton(text: "Find files", icon: "folder") {
                    viewModel.inputText = "Find all Swift files in this project"
                }
                QuickActionButton(text: "Run tests", icon: "play") {
                    viewModel.inputText = "How do I run the tests for this project?"
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(.vertical, 60)
    }

    private var loadingIndicator: some View {
        HStack(spacing: 12) {
            ProgressView()
                .scaleEffect(0.8)

            Text(viewModel.currentIteration > 1 ? "Processing tools (iteration \(viewModel.currentIteration))..." : "Thinking...")
                .font(.subheadline)
                .foregroundColor(.secondary)

            Spacer()
        }
        .padding(.horizontal)
        .padding(.vertical, 12)
        .background(Color(nsColor: .controlBackgroundColor).opacity(0.5))
        .cornerRadius(8)
    }

    private var inputArea: some View {
        HStack(alignment: .bottom, spacing: 12) {
            // Text editor
            ZStack(alignment: .topLeading) {
                if viewModel.inputText.isEmpty {
                    Text("Type a message...")
                        .foregroundColor(.secondary)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 10)
                }

                TextEditor(text: $viewModel.inputText)
                    .font(.body)
                    .scrollContentBackground(.hidden)
                    .padding(4)
                    .focused($isInputFocused)
            }
            .frame(minHeight: 40, maxHeight: 200)
            .background(Color(nsColor: .textBackgroundColor))
            .cornerRadius(8)
            .overlay(
                RoundedRectangle(cornerRadius: 8)
                    .stroke(Color.secondary.opacity(0.3), lineWidth: 1)
            )

            // Send button
            Button {
                Task {
                    await viewModel.sendMessage()
                }
            } label: {
                Image(systemName: "arrow.up.circle.fill")
                    .font(.system(size: 28))
                    .foregroundColor(viewModel.inputText.isEmpty || viewModel.isLoading ? .secondary : .accentColor)
            }
            .buttonStyle(.plain)
            .disabled(viewModel.inputText.isEmpty || viewModel.isLoading)
            .keyboardShortcut(.return, modifiers: .command)
        }
        .padding()
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private func scrollToBottom(proxy: ScrollViewProxy) {
        withAnimation(.easeOut(duration: 0.3)) {
            if viewModel.isLoading {
                proxy.scrollTo("loading", anchor: .bottom)
            } else if let lastMessage = viewModel.messages.last {
                proxy.scrollTo(lastMessage.id, anchor: .bottom)
            }
        }
    }
}

/// Quick action button for empty state
struct QuickActionButton: View {
    let text: String
    let icon: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 8) {
                Image(systemName: icon)
                    .font(.title2)
                Text(text)
                    .font(.caption)
            }
            .frame(width: 100, height: 80)
            .background(Color(nsColor: .controlBackgroundColor))
            .cornerRadius(8)
        }
        .buttonStyle(.plain)
    }
}

#Preview {
    ChatView(viewModel: ChatViewModel())
}
