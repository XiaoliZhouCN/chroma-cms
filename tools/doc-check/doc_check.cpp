// 目录文档规范检查器（doc-check）。
//
// 规则来源：AGENTS.md §12（目录规范）、docs/decisions/0001-directory-docs-convention.md。
// 由 ctest 以 `docs.structure` 运行：失败即文档规范被破坏。
//
// 检查项：
//   1. src/ tests/ tools/ 下每个含 CMakeLists.txt 的目录都有 AGENTS.md + README.md
//   2. AGENTS.md 的 YAML front-matter：extends/scope/owner/last_verified 齐全，
//      extends 指向真实父文件，scope 与目录路径一致
//   3. AGENTS.md 必须声明"继承"；有本地扩展写 "## 本地约定"，否则写"无本地扩展"占位
//   4. README.md 有标题与至少一个二级标题
//   5. docs/ 下规定目录必须有 README.md
//   6. 所有被检查 Markdown 的相对链接目标存在

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
int g_errors = 0;

void fail(const std::string &message)
{
    std::fprintf(stderr, "[doc-check] %s\n", message.c_str());
    ++g_errors;
}

std::string readFile(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    std::string text = stream.str();
    // 统一换行。
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            continue;
        }
        normalized.push_back(text[i]);
    }
    return normalized;
}

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string trim(const std::string &value)
{
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string relativePath(const fs::path &root, const fs::path &path)
{
    std::error_code error;
    const fs::path rel = fs::relative(path, root, error);
    if (error) {
        return path.generic_string();
    }
    return rel.generic_string();
}

/// 解析 AGENTS.md 的 front-matter（首个 --- 到第二个 ---）。
bool parseFrontMatter(const std::string &text, std::map<std::string, std::string> &fields,
                      std::string &body)
{
    const std::vector<std::string> lines = splitLines(text);
    if (lines.empty() || trim(lines[0]) != "---") {
        return false;
    }
    std::size_t index = 1;
    for (; index < lines.size(); ++index) {
        const std::string line = trim(lines[index]);
        if (line == "---") {
            ++index;
            break;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        fields[trim(line.substr(0, colon))] = trim(line.substr(colon + 1));
    }
    if (index > lines.size()) {
        return false;
    }
    std::string rest;
    for (std::size_t i = index; i < lines.size(); ++i) {
        rest += lines[i];
        rest.push_back('\n');
    }
    body = rest;
    return !fields.empty();
}

/// 检查 Markdown 相对链接目标是否存在。
void checkLinks(const fs::path &root, const fs::path &file)
{
    const std::string text = readFile(file);
    if (text.empty()) {
        return;
    }
    // [label](target) / [label](target "title") 都取第一个 ) 前的内容。
    const std::regex linkPattern(R"(\]\(([^)\s]+)(?:\s+"[^"]*")?\))");
    for (std::sregex_iterator it(text.begin(), text.end(), linkPattern), end; it != end; ++it) {
        std::string target = (*it)[1].str();
        if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0 ||
            target.rfind("mailto:", 0) == 0) {
            continue;
        }
        const std::size_t hash = target.find('#');
        if (hash != std::string::npos) {
            target.erase(hash);
        }
        if (target.empty() || target.rfind("file:", 0) == 0) {
            continue;
        }
        const fs::path resolved = (file.parent_path() / target).lexically_normal();
        std::error_code error;
        if (!fs::exists(resolved, error)) {
            fail(relativePath(root, file) + ": 链接目标不存在: " + target);
        }
    }
}

/// 检查单个构建目录（含 CMakeLists.txt）的 AGENTS.md / README.md。
void checkBuildDirectory(const fs::path &root, const fs::path &dir)
{
    const std::string rel = relativePath(root, dir);
    const fs::path agents = dir / "AGENTS.md";
    const fs::path readme = dir / "README.md";

    if (!fs::exists(agents)) {
        fail(rel + ": 缺少 AGENTS.md");
    }
    if (!fs::exists(readme)) {
        fail(rel + ": 缺少 README.md");
    }

    if (fs::exists(agents)) {
        const std::string text = readFile(agents);
        std::map<std::string, std::string> fields;
        std::string body;
        if (!parseFrontMatter(text, fields, body)) {
            fail(rel + "/AGENTS.md: 缺少合法 YAML front-matter（--- 包围）");
        }
        else {
            const char *required[] = {"extends", "scope", "owner", "last_verified"};
            for (const char *key : required) {
                if (fields.find(key) == fields.end() || fields[key].empty()) {
                    fail(rel + "/AGENTS.md: front-matter 缺少 " + key);
                }
            }
            const std::string scope = fields.count("scope") != 0 ? fields["scope"] : "";
            if (!scope.empty() && scope != rel) {
                fail(rel + "/AGENTS.md: scope 应为 \"" + rel + "\"，实际为 \"" + scope + "\"");
            }
            if (fields.count("extends") != 0 && !fields["extends"].empty()) {
                const fs::path parent = (dir / fields["extends"]).lexically_normal();
                std::error_code error;
                if (!fs::exists(parent, error)) {
                    fail(rel + "/AGENTS.md: extends 指向不存在: " + fields["extends"]);
                }
            }
            if (body.find("## 继承") == std::string::npos) {
                fail(rel + "/AGENTS.md: 缺少 \"## 继承\" 段");
            }
            const bool hasLocalRules = body.find("## 本地约定") != std::string::npos;
            const bool hasPlaceholder = body.find("无本地扩展") != std::string::npos;
            if (!hasLocalRules && !hasPlaceholder) {
                fail(rel + "/AGENTS.md: 需写 \"## 本地约定\"，或显式写 \"无本地扩展\" 占位");
            }
        }
    }

    if (fs::exists(readme)) {
        const std::string text = readFile(readme);
        const std::vector<std::string> lines = splitLines(text);
        if (lines.empty() || lines[0].rfind("# ", 0) != 0) {
            fail(rel + "/README.md: 必须以一级标题 \"# \" 开头");
        }
        if (text.find("\n## ") == std::string::npos) {
            fail(rel + "/README.md: 至少需要一个二级标题");
        }
    }
}

/// 递归收集包含 CMakeLists.txt 的目录。
void collectBuildDirs(const fs::path &base, std::vector<fs::path> &out)
{
    std::error_code error;
    if (!fs::exists(base, error)) {
        return;
    }
    for (fs::recursive_directory_iterator it(base, error), end; it != end; it.increment(error)) {
        if (error) {
            break;
        }
        if (!it->is_regular_file()) {
            continue;
        }
        if (it->path().filename() != "CMakeLists.txt") {
            continue;
        }
        out.push_back(it->path().parent_path());
    }
}
} // namespace

int main(int argc, char **argv)
{
    const fs::path root = (argc > 1) ? fs::absolute(argv[1]) : fs::current_path();
    if (!fs::exists(root)) {
        std::fprintf(stderr, "[doc-check] 仓库根不存在: %s\n", root.string().c_str());
        return 2;
    }

    // 1) 构建目录：AGENTS.md + README.md
    std::vector<fs::path> buildDirs;
    collectBuildDirs(root / "src", buildDirs);
    collectBuildDirs(root / "tests", buildDirs);
    collectBuildDirs(root / "tools", buildDirs);
    std::sort(buildDirs.begin(), buildDirs.end());
    buildDirs.erase(std::unique(buildDirs.begin(), buildDirs.end()), buildDirs.end());

    for (const fs::path &dir : buildDirs) {
        checkBuildDirectory(root, dir);
    }

    // 2) docs 目录：README.md（AGENTS 可选、无 CMakeLists）
    const char *docsDirs[] = {"docs", "docs/color", "docs/testing", "docs/handoff",
                              "docs/third-party", "docs/decisions", "docs/templates"};
    for (const char *rel : docsDirs) {
        const fs::path dir = root / rel;
        std::error_code error;
        if (!fs::exists(dir, error)) {
            fail(std::string(rel) + ": 目录不存在");
            continue;
        }
        if (!fs::exists(dir / "README.md")) {
            fail(std::string(rel) + ": 缺少 README.md");
        }
    }

    // 3) 相对链接检查：根文档 + docs 全量 + 各目录 README/AGENTS
    std::set<fs::path> markdownFiles;
    markdownFiles.insert(root / "README.md");
    markdownFiles.insert(root / "AGENTS.md");
    markdownFiles.insert(root / "docs" / "ROADMAP.md");
    std::error_code error;
    for (fs::recursive_directory_iterator it(root / "docs", error), end; it != end;
         it.increment(error)) {
        if (error) {
            break;
        }
        if (it->is_regular_file() && it->path().extension() == ".md") {
            markdownFiles.insert(it->path());
        }
    }
    for (const fs::path &dir : buildDirs) {
        markdownFiles.insert(dir / "README.md");
        markdownFiles.insert(dir / "AGENTS.md");
    }
    for (const fs::path &file : markdownFiles) {
        std::error_code existsError;
        if (fs::exists(file, existsError)) {
            checkLinks(root, file);
        }
    }

    if (g_errors != 0) {
        std::fprintf(stderr, "[doc-check] %d 个问题，目录规范检查失败\n", g_errors);
        return 1;
    }
    std::printf("[doc-check] ok: %zu 个构建目录、docs 目录与相对链接全部通过\n", buildDirs.size());
    return 0;
}
