#ifndef UNICODE_UTIL_H
#define UNICODE_UTIL_H

#include <string>
#include <vector>
#include "CATUnicodeString.h"

namespace UnicodeUtil
{
    std::wstring Utf8ToWString(const std::string& iUtf8);
    std::string WStringToUtf8(const std::wstring& iWide);

    CATUnicodeString WStringToCATUnicode(const std::wstring& iWide);
    CATUnicodeString Utf8ToCATUnicode(const std::string& iUtf8);
    std::string CATUnicodeToUtf8(const CATUnicodeString& iStr);
    std::wstring CATUnicodeToWString(const CATUnicodeString& iStr);

    std::vector<std::wstring> GetCommandLineArgsW();
    bool LoadArgsFromUtf8File(const std::wstring& iArgFilePath, std::vector<std::wstring>& oArgs, std::string& oErr);

    std::wstring GetParentDirW(const std::wstring& iPath);
    std::wstring GetFileNameW(const std::wstring& iPath);
    std::wstring GetFileStemW(const std::wstring& iPath);
    std::wstring GetFileExtLowerW(const std::wstring& iPath);
    std::wstring NormalizeFullPathW(const std::wstring& iPath);
    bool IsDirectoryW(const std::wstring& iPath);
    bool FileExistsW(const std::wstring& iPath);
    bool EnsureDirectoryRecursiveW(const std::wstring& iDirPath);
    bool CollectCatPartFilesW(const std::wstring& iDir, bool iRecursive, std::vector<std::wstring>& oFiles);
    std::wstring BuildRelativePathW(const std::wstring& iRootDir, const std::wstring& iFilePath);

    std::string JsonEscape(const std::string& iText);

    void LogInfo(const std::string& iMsgUtf8);
    void LogWarn(const std::string& iMsgUtf8);
    void LogError(const std::string& iMsgUtf8);
}

#endif
