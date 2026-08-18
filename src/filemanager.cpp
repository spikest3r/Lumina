#include "filemanager.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

FileManager::FileManager()
    : m_mode(Mode::OPEN), m_isOpen(false), m_hasResult(false) {
    m_saveFileNameBuffer[0] = '\0';
    m_newFolderNameBuffer[0] = '\0';
}

void FileManager::Init(Mode mode, const std::string& defaultPath, const std::vector<std::string>& filters) {
    m_mode = mode;
    m_isOpen = true;
    m_hasResult = false;
    m_resultPath = "";
    m_selectedFile = "";
    m_filters = filters;
    m_saveFileNameBuffer[0] = '\0';
    m_newFolderNameBuffer[0] = '\0';

    if (defaultPath.empty()) {
        m_currentDirectory = fs::current_path();
    } else {
        m_currentDirectory = defaultPath;
    }

    RefreshCurrentDirectory();
}

void FileManager::Update() {
    // Polling or async logic goes here if needed
}

void FileManager::RefreshCurrentDirectory() {
    m_fileList.clear();
    try {
        if (m_currentDirectory.has_parent_path() && m_currentDirectory != m_currentDirectory.parent_path()) {
            m_fileList.push_back({"..", true});
        }

        for (const auto& entry : fs::directory_iterator(m_currentDirectory)) {
            bool isDir = entry.is_directory();
            bool matchesFilter = m_filters.empty();

            // Check file extension if filters are provided and it's not a directory
            if (!isDir && !matchesFilter) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
                
                for (const auto& filter : m_filters) {
                    std::string fExt = filter;
                    std::transform(fExt.begin(), fExt.end(), fExt.begin(), [](unsigned char c){ return std::tolower(c); });
                    if (ext == fExt) {
                        matchesFilter = true;
                        break;
                    }
                }
            }

            if (isDir || matchesFilter) {
                m_fileList.push_back({
                    entry.path().filename().string(),
                    isDir
                });
            }
        }

        std::sort(m_fileList.begin(), m_fileList.end(), [](const FileInfo& a, const FileInfo& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            return a.name < b.name;
        });
    } catch (const std::exception&) {
        // Handle unreadable directories gracefully
    }
}

void FileManager::Render(const char* windowTitle) {
    if (!m_isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(windowTitle, &m_isOpen)) {

        // --- Header Section ---
        ImGui::Text("Path: %s", m_currentDirectory.string().c_str());
        
        // Push "New Folder" button to the right side
        ImGui::SameLine();
        float btnWidth = ImGui::CalcTextSize("New Folder").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - btnWidth - ImGui::GetStyle().WindowPadding.x);
        
        if (ImGui::Button("New Folder")) {
            ImGui::OpenPopup("Create New Folder");
        }

        // New Folder Modal
        if (ImGui::BeginPopupModal("Create New Folder", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Folder Name", m_newFolderNameBuffer, sizeof(m_newFolderNameBuffer));
            
            if (ImGui::Button("Create", ImVec2(120, 0))) {
                if (strlen(m_newFolderNameBuffer) > 0) {
                    try {
                        fs::create_directory(m_currentDirectory / m_newFolderNameBuffer);
                        RefreshCurrentDirectory();
                    } catch (...) { /* Handle permission/invalid name errors if needed */ }
                }
                ImGui::CloseCurrentPopup();
                m_newFolderNameBuffer[0] = '\0';
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                m_newFolderNameBuffer[0] = '\0';
            }
            ImGui::EndPopup();
        }
        
        ImGui::Separator();

        // --- Main Body: File List ---
        float bottomPanelHeight = (m_mode == Mode::SAVE) ? 60.0f : 40.0f;
        ImGui::BeginChild("FileList", ImVec2(0, -bottomPanelHeight), true);

        for (const auto& file : m_fileList) {
            std::string displayName = (file.isDirectory ? "[DIR] " : "      ") + file.name;
            bool isSelected = (m_selectedFile == file.name);

            if (ImGui::Selectable(displayName.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (file.isDirectory) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        if (file.name == "..") {
                            m_currentDirectory = m_currentDirectory.parent_path();
                        } else {
                            m_currentDirectory /= file.name;
                        }
                        RefreshCurrentDirectory();
                        m_selectedFile = "";
                        
                        ImGui::EndChild();
                        ImGui::End();
                        return;
                    }
                } else {
                    m_selectedFile = file.name;
                    if (m_mode == Mode::SAVE) {
                        strncpy(m_saveFileNameBuffer, file.name.c_str(), sizeof(m_saveFileNameBuffer) - 1);
                    }
                }
            }
        }
        ImGui::EndChild();

        // --- Footer Section ---
        if (m_mode == Mode::SAVE) {
            ImGui::InputText("File Name", m_saveFileNameBuffer, sizeof(m_saveFileNameBuffer));
        } else {
            ImGui::Text("Selected: %s", m_selectedFile.string().c_str());
        }

        // Align Action buttons to the right
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 120);

        const char* confirmText = (m_mode == Mode::OPEN) ? "Open" : "Save";
        if (ImGui::Button(confirmText, ImVec2(50, 0))) {
            if (m_mode == Mode::OPEN && !m_selectedFile.empty()) {
                m_resultPath = (m_currentDirectory / m_selectedFile).string();
                m_hasResult = true;
                m_isOpen = false;
            } else if (m_mode == Mode::SAVE && strlen(m_saveFileNameBuffer) > 0) {
                m_resultPath = (m_currentDirectory / m_saveFileNameBuffer).string();
                m_hasResult = true;
                m_isOpen = false;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(50, 0))) {
            m_isOpen = false;
        }
    }
    ImGui::End();
}