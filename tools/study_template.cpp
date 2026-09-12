#include "sierrachart.h"

SCDLLName("My Study DLL")

// Scaffold: copy via `python3 tools/sc.py new "My Study" --file MyStudy.cpp
// --study MyStudy`. MyStudy/My Study are substituted by the tool.
//
// Rules baked in from repo lessons (see STUDIES.md "Agent build notes"):
// - one scsf_ per collision family; check `tools/sc.py check` before Remote Build
// - pass SCString to const char* via .GetChars(), never implicit conversion
// - no file-scope mutable globals: per-instance state goes in
//   sc.GetPersistentInt/Fast/Float/SCString (safe with 2+ chart instances)
// - input indices are append-only once shipped; never insert/reorder

SCSFExport scsf_MyStudy(SCStudyInterfaceRef sc)
{
    SCInputRef InEnabled = sc.Input[0];
    SCInputRef InLength = sc.Input[1];

    if (sc.SetDefaults)
    {
        sc.GraphName = "My Study";
        sc.StudyDescription = "Describe what this study draws and which inputs matter.";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;
        sc.FreeDLL = 0;

        InEnabled.Name = "Enabled";
        InEnabled.SetYesNo(1);

        InLength.Name = "Length";
        InLength.SetInt(20);
        InLength.SetIntLimits(1, 500);
        return;
    }

    if (!InEnabled.GetYesNo())
        return;

    // Per-bar logic here. sc.Index is the current bar; sc.ArraySize the count.
    // Keep HTTP/order calls out of the hot path; throttle with sc.Index guards.
    const int i = sc.Index;
    (void)i;
    (void)InLength.GetInt();
}
