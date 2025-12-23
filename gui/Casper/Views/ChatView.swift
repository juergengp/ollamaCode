import SwiftUI
import AppKit

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
            // Text editor with Enter to send
            ChatInputField(
                text: $viewModel.inputText,
                placeholder: "Type a message...",
                isDisabled: viewModel.isLoading,
                onSubmit: {
                    Task {
                        await viewModel.sendMessage()
                    }
                }
            )
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

/// Custom text input that sends on Enter and inserts newline on Shift+Enter
struct ChatInputField: NSViewRepresentable {
    @Binding var text: String
    var placeholder: String
    var isDisabled: Bool
    var onSubmit: () -> Void

    func makeNSView(context: Context) -> NSScrollView {
        let scrollView = NSScrollView()
        let textView = ChatTextView()

        textView.delegate = context.coordinator
        textView.isRichText = false
        textView.font = NSFont.systemFont(ofSize: NSFont.systemFontSize)
        textView.backgroundColor = .clear
        textView.textContainerInset = NSSize(width: 8, height: 8)
        textView.isVerticallyResizable = true
        textView.isHorizontallyResizable = false
        textView.autoresizingMask = [.width]
        textView.textContainer?.widthTracksTextView = true
        textView.allowsUndo = true
        textView.placeholderString = placeholder
        textView.onSubmit = onSubmit

        scrollView.documentView = textView
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = false
        scrollView.autohidesScrollers = true
        scrollView.borderType = .noBorder
        scrollView.backgroundColor = .clear
        scrollView.drawsBackground = false

        context.coordinator.textView = textView

        return scrollView
    }

    func updateNSView(_ scrollView: NSScrollView, context: Context) {
        guard let textView = scrollView.documentView as? ChatTextView else { return }

        if textView.string != text {
            textView.string = text
        }

        textView.isEditable = !isDisabled
        textView.onSubmit = onSubmit
        textView.placeholderString = placeholder
        textView.needsDisplay = true
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(text: $text)
    }

    class Coordinator: NSObject, NSTextViewDelegate {
        var text: Binding<String>
        weak var textView: NSTextView?

        init(text: Binding<String>) {
            self.text = text
        }

        func textDidChange(_ notification: Notification) {
            guard let textView = notification.object as? NSTextView else { return }
            text.wrappedValue = textView.string
        }
    }
}

/// Custom NSTextView that handles Enter to submit
class ChatTextView: NSTextView {
    var placeholderString: String = ""
    var onSubmit: (() -> Void)?

    override func keyDown(with event: NSEvent) {
        // Check for Enter key (keyCode 36)
        if event.keyCode == 36 {
            // Shift+Enter = newline
            if event.modifierFlags.contains(.shift) {
                insertNewline(nil)
                return
            }

            // Plain Enter = submit (if there's text)
            if !self.string.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                onSubmit?()
                return
            }
        }

        super.keyDown(with: event)
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        // Draw placeholder if empty
        if string.isEmpty && !placeholderString.isEmpty {
            let attrs: [NSAttributedString.Key: Any] = [
                .foregroundColor: NSColor.placeholderTextColor,
                .font: font ?? NSFont.systemFont(ofSize: NSFont.systemFontSize)
            ]
            let placeholder = NSAttributedString(string: placeholderString, attributes: attrs)
            placeholder.draw(at: NSPoint(x: textContainerInset.width + 5, y: textContainerInset.height))
        }
    }
}

#Preview {
    ChatView(viewModel: ChatViewModel())
}
