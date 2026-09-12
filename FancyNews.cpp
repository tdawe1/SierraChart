#include "sierrachart.h"

SCDLLName("Fancy News DLL")

SCString ReadTextFile(SCStudyInterfaceRef sc, SCString FileLocation) {
    char TextBuffer[1000] = {};
    int FileHandle = 0;
    unsigned int BytesRead = 0;
    if (sc.OpenFile(FileLocation.GetChars(), n_ACSIL::FILE_MODE_OPEN_EXISTING_FOR_SEQUENTIAL_READING, FileHandle) == 0)
        return "";
    sc.ReadFile(FileHandle, TextBuffer, sizeof(TextBuffer) - 1, &BytesRead);
    sc.CloseFile(FileHandle);
    TextBuffer[sizeof(TextBuffer) - 1] = '\0';
    return TextBuffer;
}

SCSFExport scsf_FancyNews(SCStudyInterfaceRef sc) {
    std::vector<SCString> sLines;
    int i = sc.Index;
    if (i < 1)
        return;
    SCString txt = "";
    SCSubgraphRef Subgraph_Storage = sc.Subgraph[0];
    SCFloatArrayRef StorageArray = Subgraph_Storage.Arrays[0];
    SCInputRef Input_DrawLabels = sc.Input[0];
    SCInputRef News_URL = sc.Input[2];

    if (sc.SetDefaults) {
        sc.GraphName = "Fancy News";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;
        Subgraph_Storage.Name = "Storage";
        Subgraph_Storage.DrawStyle = DRAWSTYLE_IGNORE;

        Input_DrawLabels.Name = "Version";
        Input_DrawLabels.SetFloat(1.1);

        News_URL.Name = "News URL";
        News_URL.SetString("https://tradingeconomics.com/calendar");

        return;
    }

    const int ALERT_TRADERSMARTS = 26;
    const int ALERT_TRADERSMARTS_WICK = 27;

    int& RequestState = sc.GetPersistentInt(1);
    int &RecordCount = sc.GetPersistentInt(2);
    int &LastProcessedIndex = sc.GetPersistentInt(3);
    SCString &LastDate = sc.GetPersistentSCString(4);
    bool bBarClosed = sc.GetBarHasClosedStatus() == BHCS_BAR_HAS_CLOSED;
    auto close{sc.Close[i]};
    auto open{sc.Open[i]};
    auto high{sc.High[i]};
    auto low{sc.Low[i]};
    auto pclose{sc.Close[i - 1]};
    auto popen{sc.Open[i - 1]};
    auto phigh{sc.High[i - 1]};
    auto plow{sc.Low[i - 1]};
    bool bIsCurrentBar = false;
    SCString TwoMinAhead;
    SCString msg;

    if (i == sc.ArraySize - 2)
        bIsCurrentBar = true;

    if (bIsCurrentBar && bBarClosed) {
        SCDateTime CurrentTime = sc.CurrentSystemDateTime;
        SCDateTime TwoMin = CurrentTime + SCDateTime::MINUTES(2);
        SCDateTime TenMin = CurrentTime + SCDateTime::MINUTES(10);
        SCString sAMPM = "AM";
        int iHr = TwoMin.GetHour();
        if (iHr == 0) { iHr = 12; }
        else if (iHr > 12) { iHr -= 12; sAMPM = "PM"; }
        else if (iHr == 12) { sAMPM = "PM"; }

        TwoMinAhead.Format("%d:%02d %s", iHr, TwoMin.GetMinute(), sAMPM.GetChars());

        if (TwoMinAhead != LastDate) {
            LastDate = TwoMinAhead;

            SCString PathAndFileName = "C:\\temp\\today.txt";
            SCString MyInputString = ReadTextFile(sc, PathAndFileName);
            MyInputString.ParseLines(sLines);
            sc.AddMessageToLog(txt.Format("Read file, lines %d", sLines.size()), 1);

            for (const SCString &ss: sLines) {
                //sc.AddMessageToLog(msg.Format("Compare : %s %s", TwoMinAhead.GetChars(), ss.GetChars()), 1);
                if (strstr(ss.GetChars(), TwoMinAhead.GetChars())) {
                    sc.AddMessageToLog(msg.Format("News : %s %s", TwoMinAhead.GetChars(), ss.GetChars()), 1);
                    sc.AlertWithMessage(29, "2m NEWS ALERT");
                }
            }
        }

    }

}
