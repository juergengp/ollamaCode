import Foundation

extension FileManager {
    /// Get the user's home directory
    var homeDirectory: URL {
        homeDirectoryForCurrentUser
    }

    /// Get the ollamacode config directory
    var ollamacodeConfigDirectory: URL {
        homeDirectory.appendingPathComponent(".config/ollamacode")
    }

    /// Ensure the config directory exists
    func ensureConfigDirectoryExists() throws {
        try createDirectory(at: ollamacodeConfigDirectory, withIntermediateDirectories: true)
    }

    /// Check if a path is a directory
    func isDirectory(at path: String) -> Bool {
        var isDir: ObjCBool = false
        return fileExists(atPath: path, isDirectory: &isDir) && isDir.boolValue
    }

    /// Get file size in bytes
    func fileSize(at path: String) -> Int64? {
        guard let attrs = try? attributesOfItem(atPath: path),
              let size = attrs[.size] as? Int64 else {
            return nil
        }
        return size
    }

    /// Get file modification date
    func modificationDate(at path: String) -> Date? {
        guard let attrs = try? attributesOfItem(atPath: path),
              let date = attrs[.modificationDate] as? Date else {
            return nil
        }
        return date
    }

    /// Read file contents safely
    func readFile(at path: String, maxSize: Int = 10_000_000) throws -> String {
        let url = URL(fileURLWithPath: path)

        // Check file size first
        if let size = fileSize(at: path), size > maxSize {
            throw FileError.fileTooLarge(path, Int(size), maxSize)
        }

        return try String(contentsOf: url, encoding: .utf8)
    }

    /// Write file contents safely
    func writeFile(at path: String, contents: String, createDirectories: Bool = true) throws {
        let url = URL(fileURLWithPath: path)

        if createDirectories {
            let parentDir = url.deletingLastPathComponent()
            try createDirectory(at: parentDir, withIntermediateDirectories: true)
        }

        try contents.write(to: url, atomically: true, encoding: .utf8)
    }

    /// List directory contents
    func listDirectory(at path: String, recursive: Bool = false) throws -> [String] {
        if recursive {
            guard let enumerator = enumerator(atPath: path) else {
                throw FileError.notADirectory(path)
            }
            var files: [String] = []
            while let file = enumerator.nextObject() as? String {
                files.append(file)
            }
            return files
        } else {
            return try contentsOfDirectory(atPath: path)
        }
    }
}

/// File operation errors
enum FileError: LocalizedError {
    case notFound(String)
    case notADirectory(String)
    case fileTooLarge(String, Int, Int)
    case permissionDenied(String)

    var errorDescription: String? {
        switch self {
        case .notFound(let path):
            return "File not found: \(path)"
        case .notADirectory(let path):
            return "Not a directory: \(path)"
        case .fileTooLarge(let path, let size, let max):
            return "File too large: \(path) (\(size) bytes, max \(max))"
        case .permissionDenied(let path):
            return "Permission denied: \(path)"
        }
    }
}
