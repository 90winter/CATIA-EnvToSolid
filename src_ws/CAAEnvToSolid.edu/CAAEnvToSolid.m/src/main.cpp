#include <stdio.h>
#include <windows.h>
#include <objbase.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "CATSession.h"
#include "CATSessionServices.h"

#include "PartConverter.h"
#include "UnicodeUtil.h"

namespace
{

enum RunMode
{
    MODE_AUTO = 0,
    MODE_INSPECT = 1,
    MODE_CREATE_TEST = 2,
    MODE_HELP = 3
};

struct CliOptions
{
    RunMode mode;
    bool recursive;
    std::wstring positional1;
    std::wstring positional2;
    std::wstring reportPath;

    CliOptions()
        : mode(MODE_AUTO), recursive(false)
    {}
};

static void PrintUsage()
{
    UnicodeUtil::LogInfo("Usage:");
    UnicodeUtil::LogInfo("  CAAEnvToSolid.exe <input.CATPart> [output.CATPart] [--report <json>]");
    UnicodeUtil::LogInfo("  CAAEnvToSolid.exe <input_dir> [output_dir] [--recursive] [--report <json>]");
    UnicodeUtil::LogInfo("  CAAEnvToSolid.exe --inspect <file.CATPart>");
    UnicodeUtil::LogInfo("  CAAEnvToSolid.exe --create-test-part <output.CATPart>");
    UnicodeUtil::LogInfo("  CAAEnvToSolid.exe --argfile <utf8_args_file>");
}

static bool ExpandArgFileRecursive(
    const std::vector<std::wstring>& iRawArgs,
    std::vector<std::wstring>& oExpanded,
    std::string& oErr)
{
    oExpanded.clear();
    for (size_t i = 0; i < iRawArgs.size(); ++i)
    {
        if (iRawArgs[i] == L"--argfile")
        {
            if (i + 1 >= iRawArgs.size())
            {
                oErr = "Missing path after --argfile";
                return false;
            }
            std::vector<std::wstring> fileArgs;
            if (!UnicodeUtil::LoadArgsFromUtf8File(iRawArgs[i + 1], fileArgs, oErr))
            {
                return false;
            }
            for (size_t j = 0; j < fileArgs.size(); ++j)
            {
                oExpanded.push_back(fileArgs[j]);
            }
            ++i;
        }
        else
        {
            oExpanded.push_back(iRawArgs[i]);
        }
    }
    return true;
}

static bool ParseCliOptions(const std::vector<std::wstring>& iArgs, CliOptions& oOpts, std::string& oErr)
{
    oOpts = CliOptions();
    oErr.clear();

    std::vector<std::wstring> positionals;
    for (size_t i = 0; i < iArgs.size(); ++i)
    {
        const std::wstring& a = iArgs[i];
        if (a == L"-h" || a == L"--help" || a == L"/?")
        {
            oOpts.mode = MODE_HELP;
            return true;
        }
        else if (a == L"--inspect")
        {
            oOpts.mode = MODE_INSPECT;
        }
        else if (a == L"--create-test-part")
        {
            oOpts.mode = MODE_CREATE_TEST;
        }
        else if (a == L"--recursive" || a == L"-r")
        {
            oOpts.recursive = true;
        }
        else if (a == L"--report")
        {
            if (i + 1 >= iArgs.size())
            {
                oErr = "Missing path after --report";
                return false;
            }
            oOpts.reportPath = iArgs[++i];
        }
        else if (!a.empty() && a[0] == L'-')
        {
            oErr = "Unknown option: " + UnicodeUtil::WStringToUtf8(a);
            return false;
        }
        else
        {
            positionals.push_back(a);
        }
    }

    if (positionals.empty())
    {
        if (oOpts.mode == MODE_AUTO)
        {
            oOpts.mode = MODE_HELP;
            return true;
        }
        oErr = "Missing required path argument";
        return false;
    }

    if (positionals.size() > 2)
    {
        oErr = "Too many positional arguments";
        return false;
    }

    oOpts.positional1 = positionals[0];
    if (positionals.size() >= 2)
    {
        oOpts.positional2 = positionals[1];
    }
    return true;
}

static bool WriteUtf8FileW(const std::wstring& iPath, const std::string& iContent)
{
    std::wstring norm = UnicodeUtil::NormalizeFullPathW(iPath);
    UnicodeUtil::EnsureDirectoryRecursiveW(UnicodeUtil::GetParentDirW(norm));

    HANDLE hFile = ::CreateFileW(
        norm.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD written = 0;
    BOOL ok = TRUE;
    if (!iContent.empty())
    {
        ok = ::WriteFile(hFile, iContent.data(), (DWORD)iContent.size(), &written, NULL);
    }
    ::CloseHandle(hFile);
    return ok && (written == (DWORD)iContent.size());
}

static std::string BuildSingleReportJson(const ConversionSummary& s)
{
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"mode\": \"single\",\n";
    oss << "  \"status\": \"" << (s.exitCode == 0 ? "success" : (s.exitCode == 10 ? "skipped_empty" : "error")) << "\",\n";
    oss << "  \"exitCode\": " << s.exitCode << ",\n";
    oss << "  \"input\": \"" << UnicodeUtil::JsonEscape(UnicodeUtil::WStringToUtf8(s.inputPath)) << "\",\n";
    oss << "  \"output\": \"" << UnicodeUtil::JsonEscape(UnicodeUtil::WStringToUtf8(s.outputPath)) << "\",\n";
    oss << "  \"setsInspected\": " << s.totalBodiesAndSetsInspected << ",\n";
    oss << "  \"discoveredConvertible\": " << s.discoveredConvertible << ",\n";
    oss << "  \"convertedCount\": " << s.convertedCount << ",\n";
    oss << "  \"skippedDiscardedCount\": " << s.skippedDiscardedCount << ",\n";
    oss << "  \"failedCount\": " << s.failedCount << ",\n";
    oss << "  \"error\": \"" << UnicodeUtil::JsonEscape(s.errorMessage) << "\",\n";
    oss << "  \"items\": [\n";
    for (size_t i = 0; i < s.items.size(); ++i)
    {
        const ConvertedItemReport& it = s.items[i];
        oss << "    {\n";
        oss << "      \"sourceName\": \"" << UnicodeUtil::JsonEscape(it.sourceName) << "\",\n";
        oss << "      \"sourceKind\": \"" << UnicodeUtil::JsonEscape(it.sourceKind) << "\",\n";
        oss << "      \"destName\": \"" << UnicodeUtil::JsonEscape(it.destName) << "\",\n";
        oss << "      \"method\": \"" << UnicodeUtil::JsonEscape(it.method) << "\",\n";
        oss << "      \"ok\": " << (it.ok ? "true" : "false") << ",\n";
        oss << "      \"note\": \"" << UnicodeUtil::JsonEscape(it.note) << "\"\n";
        oss << "    }" << ((i + 1 < s.items.size()) ? "," : "") << "\n";
    }
    oss << "  ]\n";
    oss << "}\n";
    return oss.str();
}

static std::string BuildBatchReportJson(
    const std::wstring& iInputDir,
    const std::wstring& iOutputDir,
    bool iRecursive,
    const std::vector<ConversionSummary>& iFiles,
    int iSuccessFiles,
    int iSkippedEmptyFiles,
    int iFailedFiles)
{
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"mode\": \"batch\",\n";
    oss << "  \"recursive\": " << (iRecursive ? "true" : "false") << ",\n";
    oss << "  \"inputDir\": \"" << UnicodeUtil::JsonEscape(UnicodeUtil::WStringToUtf8(iInputDir)) << "\",\n";
    oss << "  \"outputDir\": \"" << UnicodeUtil::JsonEscape(UnicodeUtil::WStringToUtf8(iOutputDir)) << "\",\n";
    oss << "  \"totalFiles\": " << iFiles.size() << ",\n";
    oss << "  \"successFiles\": " << iSuccessFiles << ",\n";
    oss << "  \"skippedEmptyFiles\": " << iSkippedEmptyFiles << ",\n";
    oss << "  \"failedFiles\": " << iFailedFiles << ",\n";
    oss << "  \"files\": [\n";
    for (size_t i = 0; i < iFiles.size(); ++i)
    {
        const ConversionSummary& s = iFiles[i];
        oss << "    {\n";
        oss << "      \"input\": \"" << UnicodeUtil::JsonEscape(UnicodeUtil::WStringToUtf8(s.inputPath)) << "\",\n";
        oss << "      \"output\": \"" << UnicodeUtil::JsonEscape(UnicodeUtil::WStringToUtf8(s.outputPath)) << "\",\n";
        oss << "      \"exitCode\": " << s.exitCode << ",\n";
        oss << "      \"discoveredConvertible\": " << s.discoveredConvertible << ",\n";
        oss << "      \"convertedCount\": " << s.convertedCount << ",\n";
        oss << "      \"skippedDiscardedCount\": " << s.skippedDiscardedCount << ",\n";
        oss << "      \"failedCount\": " << s.failedCount << ",\n";
        oss << "      \"error\": \"" << UnicodeUtil::JsonEscape(s.errorMessage) << "\"\n";
        oss << "    }" << ((i + 1 < iFiles.size()) ? "," : "") << "\n";
    }
    oss << "  ]\n";
    oss << "}\n";
    return oss.str();
}

static std::wstring BuildDefaultSingleOutputPath(const std::wstring& iInputFile)
{
    std::wstring normIn = UnicodeUtil::NormalizeFullPathW(iInputFile);
    std::wstring parent = UnicodeUtil::GetParentDirW(normIn);
    std::wstring stem = UnicodeUtil::GetFileStemW(normIn);
    if (!parent.empty() && parent[parent.size() - 1] != L'\\' && parent[parent.size() - 1] != L'/')
    {
        parent += L"\\";
    }
    return parent + stem + L"_solid.CATPart";
}

} // anonymous namespace

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    ::SetConsoleOutputCP(65001);

    std::vector<std::wstring> rawAll = UnicodeUtil::GetCommandLineArgsW();
    std::vector<std::wstring> rawArgs;
    for (size_t i = 1; i < rawAll.size(); ++i)
    {
        rawArgs.push_back(rawAll[i]);
    }

    std::vector<std::wstring> args;
    std::string parseErr;
    if (!ExpandArgFileRecursive(rawArgs, args, parseErr))
    {
        UnicodeUtil::LogError(parseErr);
        return 1;
    }

    CliOptions opts;
    if (!ParseCliOptions(args, opts, parseErr))
    {
        UnicodeUtil::LogError(parseErr);
        PrintUsage();
        return 1;
    }

    if (opts.mode == MODE_HELP)
    {
        PrintUsage();
        return 0;
    }

    HRESULT hrCom = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);

    char sessionName[] = "CAAEnvToSolidSession";
    CATSession* pSession = NULL;
    HRESULT hrSess = ::Create_Session(sessionName, pSession);
    if (FAILED(hrSess) || pSession == NULL)
    {
        char buf[128];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "Create_Session failed (HRESULT=0x%08X)", (unsigned int)hrSess);
        UnicodeUtil::LogError(buf);
        if (SUCCEEDED(hrCom)) ::CoUninitialize();
        return 2;
    }

    int finalRc = 0;

    if (opts.mode == MODE_INSPECT)
    {
        std::string err;
        if (!InspectPartFile(opts.positional1, err))
        {
            UnicodeUtil::LogError("Inspect failed: " + err);
            finalRc = 3;
        }
    }
    else if (opts.mode == MODE_CREATE_TEST)
    {
        std::string err;
        if (!CreateTestPartFile(opts.positional1, err))
        {
            UnicodeUtil::LogError("Create test part failed: " + err);
            finalRc = 3;
        }
        else
        {
            UnicodeUtil::LogInfo("Created test CATPart: " +
                UnicodeUtil::WStringToUtf8(UnicodeUtil::NormalizeFullPathW(opts.positional1)));
        }
    }
    else
    {
        std::wstring inPath = UnicodeUtil::NormalizeFullPathW(opts.positional1);
        if (UnicodeUtil::IsDirectoryW(inPath))
        {
            std::wstring outDir = opts.positional2.empty()
                ? (inPath + L"\\output_solid")
                : UnicodeUtil::NormalizeFullPathW(opts.positional2);

            if (_wcsicmp(inPath.c_str(), outDir.c_str()) == 0)
            {
                UnicodeUtil::LogError("Output directory cannot be identical to input directory");
                finalRc = 3;
            }
            else
            {
                std::vector<std::wstring> files;
                UnicodeUtil::CollectCatPartFilesW(inPath, opts.recursive, files);

                std::vector<std::wstring> filteredFiles;
                std::wstring outDirPrefix = outDir + L"\\";
                for (size_t i = 0; i < files.size(); ++i)
                {
                    if (_wcsnicmp(files[i].c_str(), outDirPrefix.c_str(), outDirPrefix.size()) == 0)
                    {
                        continue;
                    }
                    filteredFiles.push_back(files[i]);
                }

                if (filteredFiles.empty())
                {
                    UnicodeUtil::LogWarn("No .CATPart files found in: " + UnicodeUtil::WStringToUtf8(inPath));
                    finalRc = 10;
                }
                else
                {
                    UnicodeUtil::EnsureDirectoryRecursiveW(outDir);
                    std::vector<ConversionSummary> summaries;
                    int okCount = 0;
                    int emptyCount = 0;
                    int failCount = 0;

                    for (size_t i = 0; i < filteredFiles.size(); ++i)
                    {
                        std::wstring rel = UnicodeUtil::BuildRelativePathW(inPath, filteredFiles[i]);
                        std::wstring dstFile = outDir + L"\\" + rel;
                        UnicodeUtil::LogInfo("--- Processing file [" +
                            std::to_string((unsigned long long)(i + 1)) + "/" +
                            std::to_string((unsigned long long)filteredFiles.size()) + "]: " +
                            UnicodeUtil::WStringToUtf8(filteredFiles[i]) + " ---");

                        ConversionSummary sum;
                        int rc = ConvertSinglePart(filteredFiles[i], dstFile, sum);
                        summaries.push_back(sum);
                        if (rc == 0)
                        {
                            okCount++;
                        }
                        else if (rc == 10)
                        {
                            emptyCount++;
                        }
                        else
                        {
                            failCount++;
                        }
                    }

                    char msg[256];
                    _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                        "Batch complete: total=%u success=%d skippedEmpty=%d failed=%d",
                        (unsigned)filteredFiles.size(), okCount, emptyCount, failCount);
                    UnicodeUtil::LogInfo(msg);

                    if (!opts.reportPath.empty())
                    {
                        if (!WriteUtf8FileW(
                                opts.reportPath,
                                BuildBatchReportJson(inPath, outDir, opts.recursive, summaries, okCount, emptyCount, failCount)))
                        {
                            UnicodeUtil::LogError("Failed to write batch report JSON: " +
                                UnicodeUtil::WStringToUtf8(opts.reportPath));
                            if (okCount == 0 && failCount == 0) finalRc = 14;
                            else if (failCount == 0) finalRc = 14;
                        }
                    }

                    if (finalRc != 14)
                    {
                        finalRc = (failCount > 0) ? 12 : (okCount == 0 ? 10 : 0);
                    }
                }
            }
        }
        else
        {
            std::wstring outPath = opts.positional2.empty()
                ? BuildDefaultSingleOutputPath(inPath)
                : UnicodeUtil::NormalizeFullPathW(opts.positional2);

            ConversionSummary sum;
            finalRc = ConvertSinglePart(inPath, outPath, sum);
            if (finalRc != 0 && !sum.errorMessage.empty())
            {
                UnicodeUtil::LogError(sum.errorMessage);
            }
            else
            {
                char msg[256];
                _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                    "Conversion summary: discovered=%d converted=%d discarded=%d failed=%d",
                    sum.discoveredConvertible, sum.convertedCount, sum.skippedDiscardedCount, sum.failedCount);
                UnicodeUtil::LogInfo(msg);
            }

            if (!opts.reportPath.empty())
            {
                if (!WriteUtf8FileW(opts.reportPath, BuildSingleReportJson(sum)))
                {
                    UnicodeUtil::LogError("Failed to write single report JSON: " +
                        UnicodeUtil::WStringToUtf8(opts.reportPath));
                    if (finalRc == 0)
                    {
                        finalRc = 14;
                    }
                }
            }
        }
    }

    ::Delete_Session(sessionName);
    if (SUCCEEDED(hrCom)) ::CoUninitialize();
    return finalRc;
}
