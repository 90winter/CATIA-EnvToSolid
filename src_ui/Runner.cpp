#define NOMINMAX
#define UNICODE
#define _UNICODE

#include "Runner.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace {

const UINT MSG_PROGRESS = WM_APP + 1;
const ULONGLONG WORKER_TIMEOUT_MS = 1800000ULL; // 30 minutes
static volatile LONG g_tempSeq = 0;

static void PostStage(HWND h, WPARAM stage, LPARAM val = 0)
{
    if (h) PostMessageW(h, MSG_PROGRESS, stage, val);
}

static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return "";
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static bool FileExistsW(const std::wstring& p)
{
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool DirExistsW(const std::wstring& p)
{
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring ToShortPathIfSpaces(const std::wstring& p)
{
    if (p.find(L' ') == std::wstring::npos) return p;
    DWORD n = GetShortPathNameW(p.c_str(), NULL, 0);
    if (n > 0)
    {
        std::vector<wchar_t> buf(n + 1, 0);
        DWORD k = GetShortPathNameW(p.c_str(), &buf[0], n + 1);
        if (k > 0 && k <= n)
        {
            return std::wstring(&buf[0], k);
        }
    }
    return p;
}

static std::wstring GetExeDir()
{
    wchar_t buf[32768];
    DWORD len = GetModuleFileNameW(NULL, buf, 32768);
    if (len == 0 || len >= 32768) return L".";
    std::wstring full(buf, len);
    size_t pos = full.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : full.substr(0, pos);
}

static std::wstring TrimW(const std::wstring& s)
{
    size_t a = 0;
    while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

static std::wstring ReadUtf8FileW(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return L"";
    std::string raw;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        raw.append(buf, n);
    }
    fclose(f);
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
    {
        raw.erase(0, 3);
    }
    return Utf8ToWide(raw);
}

static bool WriteUtf8LinesNoBom(const std::wstring& path, const std::vector<std::wstring>& lines)
{
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string u = WideToUtf8(lines[i]);
        if (!u.empty()) fwrite(u.data(), 1, u.size(), f);
        fwrite("\r\n", 1, 2, f);
    }
    fclose(f);
    return true;
}

static void LoadConfigIni(
    const std::wstring& configPath,
    std::wstring& ioCatStart,
    std::wstring& ioEnvName,
    std::wstring& ioEnvDir)
{
    std::wstring txt = ReadUtf8FileW(configPath);
    if (txt.empty()) return;

    size_t pos = 0;
    while (pos < txt.size())
    {
        size_t eol = txt.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = txt.size();
        std::wstring line = TrimW(txt.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos || eq == 0) continue;
        std::wstring k = TrimW(line.substr(0, eq));
        std::wstring v = TrimW(line.substr(eq + 1));
        if (_wcsicmp(k.c_str(), L"CATSTART_EXE") == 0 && !v.empty()) ioCatStart = v;
        else if (_wcsicmp(k.c_str(), L"ENV_NAME") == 0 && !v.empty()) ioEnvName = v;
        else if (_wcsicmp(k.c_str(), L"ENV_DIR") == 0 && !v.empty()) ioEnvDir = v;
    }
}

static bool LocateRuntimeEnv(
    const std::wstring& exeDir,
    std::wstring& oWorkerExe,
    std::wstring& oCatStartExe,
    std::wstring& oEnvName,
    std::wstring& oEnvDir,
    std::wstring& oErr)
{
    std::wstring distDir = exeDir + L"\\dist";
    if (FileExistsW(distDir + L"\\CAAEnvToSolid.exe"))
    {
        oWorkerExe = distDir + L"\\CAAEnvToSolid.exe";
    }
    else if (FileExistsW(exeDir + L"\\CAAEnvToSolid.exe"))
    {
        oWorkerExe = exeDir + L"\\CAAEnvToSolid.exe";
        distDir = exeDir;
    }
    else
    {
        oErr = L"找不到 CAAEnvToSolid.exe（请确认 dist\\CAAEnvToSolid.exe 存在）";
        return false;
    }

    LoadConfigIni(distDir + L"\\config.ini", oCatStartExe, oEnvName, oEnvDir);
    if (oCatStartExe.empty() || oEnvName.empty() || oEnvDir.empty())
    {
        LoadConfigIni(exeDir + L"\\config.ini", oCatStartExe, oEnvName, oEnvDir);
    }

    if (oCatStartExe.empty() || !FileExistsW(oCatStartExe))
    {
        std::vector<std::wstring> cands;
        cands.push_back(L"C:\\Program Files\\Dassault Systemes\\B26\\win_b64\\code\\bin\\CATSTART.exe");
        cands.push_back(L"D:\\Program Files\\Dassault Systemes\\B26\\win_b64\\code\\bin\\CATSTART.exe");
        const wchar_t* drives[] = { L"C:", L"D:", L"E:" };
        for (int d = 0; d < 3; ++d)
        {
            for (int v = 34; v >= 20; --v)
            {
                wchar_t b[260];
                swprintf_s(b, L"%s\\Program Files\\Dassault Systemes\\B%d\\win_b64\\code\\bin\\CATSTART.exe", drives[d], v);
                cands.push_back(b);
            }
        }
        for (size_t i = 0; i < cands.size(); ++i)
        {
            if (FileExistsW(cands[i]))
            {
                oCatStartExe = cands[i];
                break;
            }
        }
    }

    if (oCatStartExe.empty() || !FileExistsW(oCatStartExe))
    {
        oErr = L"未找到 CATSTART.exe（可在 dist\\config.ini 中设置 CATSTART_EXE）";
        return false;
    }

    if (oEnvDir.empty() || !DirExistsW(oEnvDir))
    {
        if (DirExistsW(L"C:\\ProgramData\\DassaultSystemes\\CATEnv"))
            oEnvDir = L"C:\\ProgramData\\DassaultSystemes\\CATEnv";
        else if (DirExistsW(L"D:\\ProgramData\\DassaultSystemes\\CATEnv"))
            oEnvDir = L"D:\\ProgramData\\DassaultSystemes\\CATEnv";
    }

    if (oEnvDir.empty() || !DirExistsW(oEnvDir))
    {
        oErr = L"未找到 CATEnv 目录（可在 dist\\config.ini 中设置 ENV_DIR）";
        return false;
    }

    if (oEnvName.empty())
    {
        std::wstring pattern = oEnvDir + L"\\CATIA*.txt";
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW(pattern.c_str(), &fd);
        std::wstring best;
        if (hf != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                std::wstring fn = fd.cFileName;
                size_t dot = fn.find_last_of(L'.');
                std::wstring stem = (dot == std::wstring::npos) ? fn : fn.substr(0, dot);
                if (best.empty() || stem.find(L"B26") != std::wstring::npos)
                {
                    best = stem;
                    if (stem.find(L"B26") != std::wstring::npos) break;
                }
            } while (FindNextFileW(hf, &fd));
            FindClose(hf);
        }
        oEnvName = best;
    }

    if (oEnvName.empty())
    {
        oErr = L"未找到 CATIA 环境配置文件（可在 dist\\config.ini 中设置 ENV_NAME）";
        return false;
    }
    return true;
}

static std::wstring BuildDestPath(const std::wstring& srcPath, const std::wstring& outputDir)
{
    size_t slash = srcPath.find_last_of(L"\\/");
    std::wstring srcDir = (slash == std::wstring::npos) ? L"." : srcPath.substr(0, slash);
    std::wstring fileName = (slash == std::wstring::npos) ? srcPath : srcPath.substr(slash + 1);
    size_t dot = fileName.find_last_of(L'.');
    std::wstring stem = (dot == std::wstring::npos) ? fileName : fileName.substr(0, dot);
    std::wstring ext = (dot == std::wstring::npos) ? L".CATPart" : fileName.substr(dot);

    if (outputDir.empty())
    {
        return srcDir + L"\\" + stem + L"_solid" + ext;
    }

    std::wstring outBase = outputDir;
    while (!outBase.empty() && (outBase[outBase.size() - 1] == L'\\' || outBase[outBase.size() - 1] == L'/'))
    {
        outBase.erase(outBase.size() - 1);
    }
    std::wstring candidate = outBase + L"\\" + fileName;
    if (_wcsicmp(candidate.c_str(), srcPath.c_str()) == 0)
    {
        candidate = outBase + L"\\" + stem + L"_solid" + ext;
    }
    return candidate;
}

static int ExtractJsonInt(const std::string& json, const char* key, int defaultVal)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return defaultVal;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size()) return defaultVal;
    return atoi(json.c_str() + pos);
}

static std::string ExtractJsonString(const std::string& json, const char* key)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    ++pos;
    std::string out;
    while (pos < json.size())
    {
        char c = json[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < json.size())
        {
            char esc = json[pos++];
            if (esc == 'n') out.push_back(' ');
            else if (esc == 'r') {}
            else if (esc == 't') out.push_back(' ');
            else out.push_back(esc);
        }
        else
        {
            out.push_back(c);
        }
    }
    return out;
}

static std::string ReadRawFileA(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return "";
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

} // namespace

unsigned __stdcall RunConvertJob(void* raw)
{
    ConvertJob* job = static_cast<ConvertJob*>(raw);
    if (!job) return 1;

    int exitCode = 1;
    std::wstring statusText = L"未知错误";
    std::wstring destPath = !job->outputPath.empty()
        ? job->outputPath
        : BuildDestPath(job->inputPath, job->outputDir);

    PostStage(job->notify, 0, 0);
    PostStage(job->notify, 1, 5);

    std::wstring exeDir = GetExeDir();
    std::wstring workerExe, catStartExe, envName, envDir, envErr;
    if (!LocateRuntimeEnv(exeDir, workerExe, catStartExe, envName, envDir, envErr))
    {
        if (job->outExitCode) *job->outExitCode = 2;
        if (job->outStatus) *job->outStatus = envErr;
        if (job->outDestPath) *job->outDestPath = destPath;
        PostStage(job->notify, 4, 0);
        delete job;
        return 2;
    }

    wchar_t tempDir[MAX_PATH + 1] = {0};
    GetTempPathW(MAX_PATH, tempDir);
    ULONGLONG tick = GetTickCount64();
    DWORD pid = GetCurrentProcessId();
    LONG seq = InterlockedIncrement(&g_tempSeq);

    wchar_t argPath[MAX_PATH + 96];
    wchar_t repPath[MAX_PATH + 96];
    swprintf_s(argPath, L"%scaa_ui_args_%lu_%llu_%ld.txt", tempDir, (unsigned long)pid, (unsigned long long)tick, seq);
    swprintf_s(repPath, L"%scaa_ui_rep_%lu_%llu_%ld.json", tempDir, (unsigned long)pid, (unsigned long long)tick, seq);

    std::vector<std::wstring> argLines;
    argLines.push_back(job->inputPath);
    argLines.push_back(destPath);
    argLines.push_back(L"--report");
    argLines.push_back(repPath);

    if (!WriteUtf8LinesNoBom(argPath, argLines))
    {
        if (job->outExitCode) *job->outExitCode = 3;
        if (job->outStatus) *job->outStatus = L"无法写入临时参数文件";
        if (job->outDestPath) *job->outDestPath = destPath;
        PostStage(job->notify, 4, 0);
        delete job;
        return 3;
    }

    std::wstring shortWorker = ToShortPathIfSpaces(workerExe);
    std::wstring shortArg = ToShortPathIfSpaces(argPath);
    std::wstring runArg = shortWorker + L" --argfile " + shortArg;
    std::wstring cmdLine = L"\"" + catStartExe + L"\" -env \"" + envName + L"\" -direnv \"" + envDir +
                           L"\" -run \"" + runArg + L"\" -nowindow";

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
    if (hReadPipe) SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;

    HANDLE hJob = CreateJobObjectW(NULL, NULL);
    if (hJob)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    PostStage(job->notify, 1, 15);

    BOOL created = CreateProcessW(
        NULL,
        &cmdBuf[0],
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        NULL,
        NULL,
        &si,
        &pi);

    if (hWritePipe)
    {
        CloseHandle(hWritePipe);
        hWritePipe = NULL;
    }

    if (!created)
    {
        if (hJob) CloseHandle(hJob);
        if (hReadPipe) CloseHandle(hReadPipe);
        DeleteFileW(argPath);
        if (job->outExitCode) *job->outExitCode = 4;
        if (job->outStatus) *job->outStatus = L"启动 CATSTART.exe 失败";
        if (job->outDestPath) *job->outDestPath = destPath;
        PostStage(job->notify, 4, 0);
        delete job;
        return 4;
    }

    bool assignedToJob = false;
    if (hJob)
    {
        assignedToJob = (AssignProcessToJobObject(hJob, pi.hProcess) != FALSE);
    }
    ResumeThread(pi.hThread);

    bool canceled = false;
    bool timedOut = false;
    ULONGLONG startTick = GetTickCount64();
    int curPct = 20;
    PostStage(job->notify, 1, curPct);

    std::string logOut;
    char pipeBuf[2048];
    for (;;)
    {
        if (job->cancelEvent && WaitForSingleObject(job->cancelEvent, 0) == WAIT_OBJECT_0)
        {
            canceled = true;
            if (hJob && assignedToJob) TerminateJobObject(hJob, 1);
            else TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            break;
        }
        if (GetTickCount64() - startTick > WORKER_TIMEOUT_MS)
        {
            timedOut = true;
            if (hJob && assignedToJob) TerminateJobObject(hJob, 1);
            else TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            break;
        }

        DWORD avail = 0;
        if (hReadPipe && PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail, NULL) && avail > 0)
        {
            DWORD toRead = (avail < sizeof(pipeBuf) - 1) ? avail : (DWORD)(sizeof(pipeBuf) - 1);
            DWORD readBytes = 0;
            if (ReadFile(hReadPipe, pipeBuf, toRead, &readBytes, NULL) && readBytes > 0)
            {
                logOut.append(pipeBuf, readBytes);
                if (curPct < 88)
                {
                    curPct += 8;
                    if (curPct > 88) curPct = 88;
                    PostStage(job->notify, 1, curPct);
                }
            }
        }

        DWORD wr = WaitForSingleObject(pi.hProcess, 80);
        if (wr == WAIT_OBJECT_0)
        {
            // Drain remaining pipe output
            if (hReadPipe)
            {
                for (;;)
                {
                    DWORD rem = 0;
                    if (!PeekNamedPipe(hReadPipe, NULL, 0, NULL, &rem, NULL) || rem == 0) break;
                    DWORD rbytes = 0;
                    if (!ReadFile(hReadPipe, pipeBuf, sizeof(pipeBuf) - 1, &rbytes, NULL) || rbytes == 0) break;
                    logOut.append(pipeBuf, rbytes);
                }
            }
            break;
        }
    }

    DWORD procExit = 1;
    GetExitCodeProcess(pi.hProcess, &procExit);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (hJob) CloseHandle(hJob);
    if (hReadPipe) CloseHandle(hReadPipe);

    std::string repJson = ReadRawFileA(repPath);
    DeleteFileW(argPath);
    DeleteFileW(repPath);

    bool hasValidReceipt = (!repJson.empty() &&
                            repJson.find("\"mode\"") != std::string::npos &&
                            ExtractJsonInt(repJson, "exitCode", -9999) != -9999);

    if (canceled)
    {
        exitCode = 1;
        statusText = L"已取消";
        PostStage(job->notify, 5, 0);
    }
    else if (timedOut)
    {
        exitCode = 1;
        statusText = L"超时中止";
        PostStage(job->notify, 4, 0);
    }
    else if (hasValidReceipt)
    {
        int repEc = ExtractJsonInt(repJson, "exitCode", (int)procExit);
        int conv = ExtractJsonInt(repJson, "convertedCount", 0);
        int disc = ExtractJsonInt(repJson, "skippedDiscardedCount", 0);
        int fail = ExtractJsonInt(repJson, "failedCount", 0);
        std::string errUtf8 = ExtractJsonString(repJson, "error");
        exitCode = repEc;

        wchar_t sbuf[512];
        if (repEc == 0)
        {
            if (!FileExistsW(destPath))
            {
                exitCode = 16;
                statusText = L"失败: 回执显示成功但未生成输出文件";
                PostStage(job->notify, 4, 0);
            }
            else
            {
                swprintf_s(sbuf, L"成功 (转换 %d，舍弃 %d)", conv, disc);
                statusText = sbuf;
                PostStage(job->notify, 1, 100);
                PostStage(job->notify, 3, 0);
            }
        }
        else if (repEc == 10)
        {
            swprintf_s(sbuf, L"跳过 (无显示可转实体，舍弃 %d)", disc);
            statusText = sbuf;
            PostStage(job->notify, 4, 0);
        }
        else if (repEc == 11)
        {
            if (!FileExistsW(destPath))
            {
                exitCode = 16;
                statusText = L"失败: 部分转换成功但未生成输出文件";
                PostStage(job->notify, 4, 0);
            }
            else
            {
                swprintf_s(sbuf, L"部分成功 (成功 %d，失败 %d，已生成输出)", conv, fail);
                statusText = sbuf;
                PostStage(job->notify, 4, 0);
            }
        }
        else
        {
            std::wstring errW = Utf8ToWide(errUtf8);
            if (errW.empty())
            {
                swprintf_s(sbuf, L"失败 (exitCode=%d)", repEc);
                statusText = sbuf;
            }
            else
            {
                statusText = L"失败: " + errW;
            }
            PostStage(job->notify, 4, 0);
        }
    }
    else
    {
        exitCode = (procExit != 0) ? (int)procExit : 15;
        wchar_t sbuf[256];
        swprintf_s(sbuf, L"失败: 未生成有效回执 (进程退出码=%lu)", (unsigned long)procExit);
        statusText = sbuf;
        PostStage(job->notify, 4, 0);
    }

    if (job->outExitCode) *job->outExitCode = exitCode;
    if (job->outStatus) *job->outStatus = statusText;
    if (job->outDestPath) *job->outDestPath = destPath;

    delete job;
    return (unsigned)exitCode;
}
