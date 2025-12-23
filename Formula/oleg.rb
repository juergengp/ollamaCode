class Oleg < Formula
  desc "Agentic AI assistant powered by local LLMs"
  homepage "https://github.com/juergengp/ollamaCode"
  version "2.3.1"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/juergengp/ollamaCode/raw/main/bin/oleg-arm64"
      sha256 "e11feb5e853ab864555680a0f15be75320912517f7c4649fa3a39e3e41fbaec5"
    else
      url "https://github.com/juergengp/ollamaCode/raw/main/bin/oleg-x86_64"
      sha256 "877afacaf5880c47b9283bbf8c8f4ac3cb81b5594f76f02dad2e04d15da6526d"
    end
  end

  depends_on :macos

  def install
    binary_name = Hardware::CPU.arm? ? "oleg-macos-arm64" : "oleg-macos-x86_64"
    bin.install Dir["*"].first => "oleg"
  end

  test do
    assert_match "OlEg version", shell_output("#{bin}/oleg --version")
  end
end
