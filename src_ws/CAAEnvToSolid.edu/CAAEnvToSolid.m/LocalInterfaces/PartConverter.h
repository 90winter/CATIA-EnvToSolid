#ifndef PART_CONVERTER_H
#define PART_CONVERTER_H

#include <string>
#include <vector>

struct ConvertedItemReport
{
    std::string sourceName;
    std::string sourceKind;
    std::string destName;
    std::string method;
    bool ok;
    std::string note;

    ConvertedItemReport()
        : ok(false)
    {}
};

struct ConversionSummary
{
    std::wstring inputPath;
    std::wstring outputPath;
    int totalBodiesAndSetsInspected;
    int discoveredConvertible;
    int convertedCount;
    int skippedDiscardedCount;
    int failedCount;
    int exitCode;
    std::string errorMessage;
    std::vector<ConvertedItemReport> items;

    ConversionSummary()
        : totalBodiesAndSetsInspected(0),
          discoveredConvertible(0),
          convertedCount(0),
          skippedDiscardedCount(0),
          failedCount(0),
          exitCode(1)
    {}
};

bool InspectPartFile(const std::wstring& iPath, std::string& oErr);

int ConvertSinglePart(
    const std::wstring& iSrcPath,
    const std::wstring& iDstPath,
    ConversionSummary& oSummary);

bool CreateTestPartFile(const std::wstring& iDstPath, std::string& oErr);

#endif
