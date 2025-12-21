import SwiftUI

/// Agent selector popover view
struct AgentSelectorView: View {
    @Binding var selectedAgent: AgentType
    @Environment(\.dismiss) var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Select Agent")
                .font(.headline)
                .padding()

            Divider()

            ScrollView {
                VStack(spacing: 8) {
                    ForEach(AgentType.allCases) { agent in
                        AgentRowView(
                            agent: agent,
                            isSelected: agent == selectedAgent
                        ) {
                            selectedAgent = agent
                            dismiss()
                        }
                    }
                }
                .padding()
            }
        }
        .frame(width: 320, height: 400)
    }
}

/// Individual agent row
struct AgentRowView: View {
    let agent: AgentType
    let isSelected: Bool
    let onSelect: () -> Void

    var body: some View {
        Button(action: onSelect) {
            HStack(alignment: .top, spacing: 12) {
                // Icon
                ZStack {
                    Circle()
                        .fill(agent.color.opacity(0.2))
                        .frame(width: 44, height: 44)

                    Image(systemName: agent.icon)
                        .font(.system(size: 20))
                        .foregroundColor(agent.color)
                }

                // Content
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text(agent.emoji)
                        Text(agent.displayName)
                            .font(.headline)

                        Spacer()

                        if isSelected {
                            Image(systemName: "checkmark.circle.fill")
                                .foregroundColor(.accentColor)
                        }
                    }

                    Text(agent.description)
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .lineLimit(2)

                    // Tool badges
                    HStack(spacing: 4) {
                        ForEach(Array(agent.allowedTools).sorted(by: { $0.rawValue < $1.rawValue }), id: \.self) { tool in
                            Text(tool.rawValue)
                                .font(.caption2)
                                .padding(.horizontal, 6)
                                .padding(.vertical, 2)
                                .background(Color.secondary.opacity(0.2))
                                .cornerRadius(4)
                        }
                    }
                }
            }
            .padding(12)
            .background(isSelected ? agent.color.opacity(0.1) : Color.clear)
            .cornerRadius(8)
            .overlay(
                RoundedRectangle(cornerRadius: 8)
                    .stroke(isSelected ? agent.color : Color.clear, lineWidth: 2)
            )
        }
        .buttonStyle(.plain)
    }
}

/// Compact agent badge for display
struct AgentBadgeView: View {
    let agent: AgentType

    var body: some View {
        HStack(spacing: 6) {
            Image(systemName: agent.icon)
                .font(.caption)

            Text(agent.displayName)
                .font(.caption)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(agent.color.opacity(0.2))
        .foregroundColor(agent.color)
        .cornerRadius(12)
    }
}

/// Agent info tooltip
struct AgentInfoView: View {
    let agent: AgentType

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(agent.emoji)
                    .font(.title)

                VStack(alignment: .leading) {
                    Text(agent.displayName)
                        .font(.headline)

                    Text("Temperature: \(String(format: "%.1f", agent.suggestedTemperature))")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }

            Text(agent.description)
                .font(.body)

            Divider()

            VStack(alignment: .leading, spacing: 8) {
                Text("Available Tools")
                    .font(.subheadline.weight(.medium))

                LazyVGrid(columns: [GridItem(.adaptive(minimum: 80))], spacing: 8) {
                    ForEach(Array(agent.allowedTools).sorted(by: { $0.rawValue < $1.rawValue }), id: \.self) { tool in
                        HStack(spacing: 4) {
                            Image(systemName: tool.icon)
                                .font(.caption)
                            Text(tool.rawValue)
                                .font(.caption)
                        }
                        .padding(.horizontal, 8)
                        .padding(.vertical, 4)
                        .background(Color.secondary.opacity(0.1))
                        .cornerRadius(6)
                    }
                }
            }
        }
        .padding()
        .frame(width: 280)
    }
}

#Preview {
    VStack {
        AgentSelectorView(selectedAgent: .constant(.general))

        AgentBadgeView(agent: .coder)

        AgentInfoView(agent: .explorer)
    }
}
