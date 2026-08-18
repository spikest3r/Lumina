#pragma once
#include <string>
#include <filesystem>
#include <vector>

class FileManager {
public:
    enum class Mode {
        OPEN,
        SAVE
    };

    FileManager();
    ~FileManager() = default;

    // Initializes the file manager. 
    // Pass filters like {".json", ".txt"} to restrict visible files. Leave empty to show all.
    void Init(Mode mode, const std::string& defaultPath = "", const std::vector<std::string>& filters = {});
    
    void Update();
    void Render(const char* windowTitle = "File Explorer");

    bool IsOpen() const { return m_isOpen; }
    void Close() { m_isOpen = false; }
    void Open() { m_isOpen = true; }

    bool HasResult() const { return m_hasResult; }
    std::string GetResult() const { return m_resultPath; }
    void ClearResult() { m_hasResult = false; m_resultPath.clear(); }

private:
    void RefreshCurrentDirectory();

    Mode m_mode;
    bool m_isOpen;
    bool m_hasResult;
    std::string m_resultPath;

    std::filesystem::path m_currentDirectory;
    std::filesystem::path m_selectedFile;
    std::vector<std::string> m_filters;

    char m_saveFileNameBuffer[256];
    char m_newFolderNameBuffer[256];

    struct FileInfo {
        std::string name;
        bool isDirectory;
    };
    std::vector<FileInfo> m_fileList;
};