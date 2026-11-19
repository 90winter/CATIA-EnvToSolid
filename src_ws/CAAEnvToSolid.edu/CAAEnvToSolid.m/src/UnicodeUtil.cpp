#include "UnicodeUtil.h"

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace UnicodeUtil
{

std::wstring Utf8ToWString(const std::string& iUtf8)
{
    if (iUtf8.empty())
    {
        return std::wstring();
    }
    int len = ::MultiByteToWideChar(CP_UTF8, 0, iUtf8.c_str(), (int)iUtf8.size(), NULL, 0);
    if (len <= 0)
    {
        return std::wstring();
    }
    std::wstring out((size_t)len, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, iUtf8.c_str(), (int)iUtf8.size(), &out[0], len);
    return out;
}

std::string WStringToUtf8(const std::wstring& iWide)
{
    if (iWide.empty())
    {
        return std::string();
    }
    int len = ::WideCharToMultiByte(CP_UTF8, 0, iWide.c_str(), (int)iWide.size(), NULL, 0, NULL, NULL);
    if (len <= 0)
    {
        return std::string();
    }
    std::string out((size_t)len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, iWide.c_str(), (int)iWide.size(), &out[0], len, NULL, NULL);
    return out;
}

CATUnicodeString WStringToCATUnicode(const std::wstring& iWide)
{
    CATUnicodeString res;
    if (!iWide.empty())
    {
        res.BuildFromWChar(iWide.c_str());
    }
    return res;
}

CATUnicodeString Utf8ToCATUnicode(const std::string& iUtf8)
{
    CATUnicodeString res;
    if (!iUtf8.empty())
    {
        res.BuildFromUTF8(iUtf8.c_str(), (size_t)iUtf8.size());
    }
    return res;
}

std::string CATUnicodeToUtf8(const CATUnicodeString& iStr)
{
    int nbChar = iStr.GetLengthInChar();
    if (nbChar <= 0)
    {
        return std::string();
    }
    std::vector<char> buf((size_t)nbChar * 4 + 8, '\0');
    size_t byteCount = 0;
    iStr.ConvertToUTF8(&buf[0], &byteCount);
    if (byteCount >= buf.size())
    {
        byteCount = buf.size() - 1;
    }
    buf[byteCount] = '\0';
    return std::string(&buf[0], byteCount);
}

std::wstring CATUnicodeToWString(const CATUnicodeString& iStr)
{
    int nbChar = iStr.GetLengthInChar();
    if (nbChar <= 0)
    {
        return std::wstring();
    }
    std::vector<wchar_t> buf((size_t)nbChar * 2 + 4, L'\0');
    iStr.ConvertToWChar(&buf[0]);
    return std::wstring(&buf[0]);
}

std::vector<std::wstring> GetCommandLineArgsW()
{
    std::vector<std::wstring> args;
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == NULL)
    {
        return args;
    }
    for (int i = 0; i < argc; ++i)
    {
        args.push_back(argv[i] ? std::wstring(argv[i]) : std::wstring());
    }
    ::LocalFree(argv);
    return args;
}

bool LoadArgsFromUtf8File(const std::wstring& iArgFilePath, std::vector<std::wstring>& oArgs, std::string& oErr)
{
    oArgs.clear();
    oErr.clear();

    HANDLE hFile = ::CreateFileW(
        iArgFilePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        DWORD gle = ::GetLastError();
        char msg[256];
        _snprintf_s(msg, sizeof(msg), _TRUNCATE, "Cannot open argfile (WinError=%lu)", (unsigned long)gle);
        oErr = msg;
        return false;
    }

    LARGE_INTEGER liSize;
    if (!::GetFileSizeEx(hFile, &liSize) || liSize.QuadPart < 0 || liSize.QuadPart > 16 * 1024 * 1024)
    {
        ::CloseHandle(hFile);
        oErr = "Invalid argfile size";
        return false;
    }

    std::string content((size_t)liSize.QuadPart, '\0');
    if (liSize.QuadPart > 0)
    {
        DWORD totalRead = 0;
        while (totalRead < (DWORD)liSize.QuadPart)
        {
            DWORD chunk = 0;
            if (!::ReadFile(hFile, &content[totalRead], (DWORD)liSize.QuadPart - totalRead, &chunk, NULL) || chunk == 0)
            {
                break;
            }
            totalRead += chunk;
        }
        content.resize(totalRead);
    }
    ::CloseHandle(hFile);

    // Strip optional UTF-8 BOM
    if (content.size() >= 3 &&
        (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB &&
        (unsigned char)content[2] == 0xBF)
    {
        content.erase(0, 3);
    }

    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r')
        {
            line.erase(line.size() - 1);
        }
        if (line.empty())
        {
            continue;
        }
        oArgs.push_back(Utf8ToWString(line));
    }
    return true;
}

std::wstring GetParentDirW(const std::wstring& iPath)
{
    size_t pos = iPath.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return L".";
    }
    if (pos == 2 && iPath[1] == L':')
    {
        return iPath.substr(0, 3);
    }
    return iPath.substr(0, pos);
}

std::wstring GetFileNameW(const std::wstring& iPath)
{
    size_t pos = iPath.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return iPath;
    }
    return iPath.substr(pos + 1);
}

std::wstring GetFileStemW(const std::wstring& iPath)
{
    std::wstring name = GetFileNameW(iPath);
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos)
    {
        return name;
    }
    return name.substr(0, dot);
}

std::wstring GetFileExtLowerW(const std::wstring& iPath)
{
    std::wstring name = GetFileNameW(iPath);
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos)
    {
        return std::wstring();
    }
    std::wstring ext = name.substr(dot);
    for (size_t i = 0; i < ext.size(); ++i)
    {
        ext[i] = (wchar_t)towlower(ext[i]);
    }
    return ext;
}

std::wstring NormalizeFullPathW(const std::wstring& iPath)
{
    if (iPath.empty())
    {
        return std::wstring();
    }
    DWORD needed = ::GetFullPathNameW(iPath.c_str(), 0, NULL, NULL);
    if (needed == 0)
    {
        return iPath;
    }
    std::vector<wchar_t> buf((size_t)needed + 2, L'\0');
    DWORD written = ::GetFullPathNameW(iPath.c_str(), (DWORD)buf.size(), &buf[0], NULL);
    if (written == 0)
    {
        return iPath;
    }
    std::wstring res(&buf[0], written);
    while (res.size() > 3 && (res[res.size() - 1] == L'\\' || res[res.size() - 1] == L'/'))
    {
        res.erase(res.size() - 1);
    }
    return res;
}

bool IsDirectoryW(const std::wstring& iPath)
{
    DWORD attr = ::GetFileAttributesW(iPath.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

bool FileExistsW(const std::wstring& iPath)
{
    DWORD attr = ::GetFileAttributesW(iPath.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

bool EnsureDirectoryRecursiveW(const std::wstring& iDirPath)
{
    if (iDirPath.empty())
    {
        return false;
    }
    std::wstring norm = NormalizeFullPathW(iDirPath);
    if (IsDirectoryW(norm))
    {
        return true;
    }

    size_t pos = 0;
    if (norm.size() >= 2 && norm[1] == L':')
    {
        pos = 3;
    }
    else if (norm.size() >= 2 && norm[0] == L'\\' && norm[1] == L'\\')
    {
        size_t s1 = norm.find(L'\\', 2);
        if (s1 != std::wstring::npos)
        {
            size_t s2 = norm.find(L'\\', s1 + 1);
            if (s2 != std::wstring::npos) pos = s2 + 1;
        }
    }

    while (pos <= norm.size())
    {
        size_t nextSlash = norm.find_first_of(L"\\/", pos);
        std::wstring sub = (nextSlash == std::wstring::npos) ? norm : norm.substr(0, nextSlash);
        if (!sub.empty() && !IsDirectoryW(sub))
        {
            if (!::CreateDirectoryW(sub.c_str(), NULL))
            {
                DWORD err = ::GetLastError();
                if (err != ERROR_ALREADY_EXISTS)
                {
                    return false;
                }
            }
        }
        if (nextSlash == std::wstring::npos)
        {
            break;
        }
        pos = nextSlash + 1;
    }
    return IsDirectoryW(norm);
}

static void CollectCatPartFilesImplW(
    const std::wstring& iDir,
    bool iRecursive,
    std::vector<std::wstring>& oFiles)
{
    std::wstring pattern = iDir;
    if (!pattern.empty() && pattern[pattern.size() - 1] != L'\\' && pattern[pattern.size() - 1] != L'/')
    {
        pattern += L"\\";
    }
    pattern += L"*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = ::FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return;
    }

    std::vector<std::wstring> subDirs;
    do
    {
        std::wstring name(fd.cFileName);
        if (name == L"." || name == L"..")
        {
            continue;
        }
        std::wstring full = iDir;
        if (!full.empty() && full[full.size() - 1] != L'\\' && full[full.size() - 1] != L'/')
        {
            full += L"\\";
        }
        full += name;

        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            if (iRecursive)
            {
                subDirs.push_back(full);
            }
        }
        else
        {
            if (GetFileExtLowerW(name) == L".catpart")
            {
                oFiles.push_back(full);
            }
        }
    } while (::FindNextFileW(hFind, &fd));

    ::FindClose(hFind);

    std::sort(subDirs.begin(), subDirs.end());
    for (size_t i = 0; i < subDirs.size(); ++i)
    {
        CollectCatPartFilesImplW(subDirs[i], iRecursive, oFiles);
    }
}

bool CollectCatPartFilesW(const std::wstring& iDir, bool iRecursive, std::vector<std::wstring>& oFiles)
{
    oFiles.clear();
    if (!IsDirectoryW(iDir))
    {
        return false;
    }
    std::wstring normDir = NormalizeFullPathW(iDir);
    CollectCatPartFilesImplW(normDir, iRecursive, oFiles);
    std::sort(oFiles.begin(), oFiles.end());
    return true;
}

std::wstring BuildRelativePathW(const std::wstring& iRootDir, const std::wstring& iFilePath)
{
    std::wstring normRoot = NormalizeFullPathW(iRootDir);
    std::wstring normFile = NormalizeFullPathW(iFilePath);
    if (normRoot.size() < normFile.size())
    {
        if (_wcsnicmp(normRoot.c_str(), normFile.c_str(), normRoot.size()) == 0)
        {
            size_t idx = normRoot.size();
            if (normFile[idx] == L'\\' || normFile[idx] == L'/')
            {
                return normFile.substr(idx + 1);
            }
        }
    }
    return GetFileNameW(normFile);
}

std::string JsonEscape(const std::string& iText)
{
    std::string out;
    out.reserve(iText.size() + 16);
    for (size_t i = 0; i < iText.size(); ++i)
    {
        unsigned char c = (unsigned char)iText[i];
        switch (c)
        {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char hex[8];
                _snprintf_s(hex, sizeof(hex), _TRUNCATE, "\\u%04x", (unsigned int)c);
                out += hex;
            }
            else
            {
                out += (char)c;
            }
            break;
        }
    }
    return out;
}

static void LogImpl(const char* iLevel, const std::string& iMsgUtf8)
{
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    fprintf(stdout, "[%02u:%02u:%02u] [%s] %s\n",
        (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
        iLevel, iMsgUtf8.c_str());
    fflush(stdout);
}

void LogInfo(const std::string& iMsgUtf8) { LogImpl("INFO", iMsgUtf8); }
void LogWarn(const std::string& iMsgUtf8) { LogImpl("WARN", iMsgUtf8); }
void LogError(const std::string& iMsgUtf8) { LogImpl("ERROR", iMsgUtf8); }

} // namespace UnicodeUtil
