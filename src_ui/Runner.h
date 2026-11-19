#pragma once
#include <windows.h>
#include <string>

struct ConvertJob {
    std::wstring inputPath;
    std::wstring outputDir;  // used when outputPath is empty
    std::wstring outputPath; // explicit destination path (preferred)
    HWND notify;
    HANDLE cancelEvent;
    int* outExitCode;
    std::wstring* outStatus;
    std::wstring* outDestPath;

    ConvertJob()
        : notify(NULL), cancelEvent(NULL), outExitCode(NULL), outStatus(NULL), outDestPath(NULL)
    {}
};

// WM_APP+1 wParam stages:
//   0 = Start
//   1 = Progress (lParam = percentage 0..100)
//   3 = Success
//   4 = Failure
//   5 = Canceled
unsigned __stdcall RunConvertJob(void* raw);
