#include "sierrachart.h"

SCDLLName("VolImb RENKO DLL")

    bool IsGreen(SCBaseDataRef InData, int index)
{
    return InData[SC_LAST][index] > InData[SC_OPEN][index];
}

bool IsRed(SCBaseDataRef InData, int index)
{
    return InData[SC_LAST][index] < InData[SC_OPEN][index];
}

bool IsVolImbGreen(SCStudyInterfaceRef sc, int index)
{
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (IsGreen(InData, index) && IsGreen(InData, index - 1) && InData[SC_OPEN][index] > InData[SC_LAST][index - 1])
        ret_flag = true;

    return ret_flag;
}

bool IsVolImbRed(SCStudyInterfaceRef sc, int index)
{
    SCBaseDataRef InData = sc.BaseData;
    bool ret_flag = false;

    if (IsRed(InData, index) && IsRed(InData, index - 1) && InData[SC_OPEN][index] < InData[SC_LAST][index - 1])
        ret_flag = true;

    return ret_flag;
}

static std::string EscapeDiscordJson(const std::string &In)
{
    std::string Out;
    Out.reserve(In.size() + 2);
    for (size_t i = 0; i < In.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(In[i]);
        switch (c)
        {
            case '"':  Out += "\\\""; break;
            case '\\': Out += "\\\\"; break;
            case '\n': Out += "\\n"; break;
            case '\r': Out += "\\r"; break;
            case '\t': Out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    static const char *Hex = "0123456789abcdef";
                    Out += "\\u00";
                    Out += Hex[(c >> 4) & 0xF];
                    Out += Hex[c & 0xF];
                }
                else
                    Out += In[i];
                break;
        }
    }
    return Out;
}

void SendDiscordWebhook(SCStudyInterfaceRef sc, const std::string &webhookUrl, const std::string &message)
{
    std::stringstream jsonPayload;
    jsonPayload << "{\"content\":\"" << EscapeDiscordJson(message) << "\"}";

    n_ACSIL::s_HTTPHeader HTTPHeader;
    HTTPHeader.Name = "Content-Type";
    HTTPHeader.Value = "application/json";

    sc.MakeHTTPPOSTRequest(
        webhookUrl.c_str(),
        jsonPayload.str().c_str(),
        &HTTPHeader,
        1 // Number of headers
    );
}

static void MaybeSendWebhook(SCStudyInterfaceRef sc, const SCString& Url, const SCString& Text)
{
    if (Url.GetLength() <= 0)
        return;
    SCString Msg;
    Msg.Format("%s %s", sc.GetChartSymbol(sc.ChartNumber).GetChars(), Text.GetChars());
    SendDiscordWebhook(sc, std::string(Url.GetChars()), std::string(Msg.GetChars()));
}

SCSFExport scsf_VolImbRenko(SCStudyInterfaceRef sc)
{
    SCString txt;

    SCInputRef Input_WebhookURL = sc.Input[0];
    SCInputRef Input_RotationMinTicks = sc.Input[1];
    SCInputRef Input_MinRotDensity = sc.Input[2];
    SCInputRef Input_BarColorWaddah = sc.Input[12];
    SCInputRef Input_BarColorLinda = sc.Input[13];

    SCSubgraphRef Subgraph_DotUp = sc.Subgraph[0];
    SCSubgraphRef Subgraph_DotDown = sc.Subgraph[1];
    SCSubgraphRef Subgraph_VolImbUp = sc.Subgraph[2];
    SCSubgraphRef Subgraph_VolImbDown = sc.Subgraph[3];
    SCSubgraphRef Subgraph_RotDelta = sc.Subgraph[4];
    SCSubgraphRef Subgraph_RotDensity = sc.Subgraph[5];

    SCSubgraphRef Subgraph_ColorBar = sc.Subgraph[17];
    SCSubgraphRef Subgraph_ColorUp = sc.Subgraph[18];
    SCSubgraphRef Subgraph_ColorDown = sc.Subgraph[19];
    SCSubgraphRef Subgraph_Calc = sc.Subgraph[27];
    SCSubgraphRef Subgraph_Intersection = sc.Subgraph[32];

    COLORREF UpColor = Subgraph_ColorUp.PrimaryColor;
    COLORREF DownColor = Subgraph_ColorDown.PrimaryColor;

    if (sc.SetDefaults)
    {
        sc.GraphName = "VolImb RENKO";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;

        Subgraph_ColorBar.Name = "Bar Color";
        Subgraph_ColorBar.DrawStyle = DRAWSTYLE_COLOR_BAR;
        Subgraph_ColorBar.LineWidth = 1;

        Subgraph_ColorUp.Name = "Bar Color Up";
        Subgraph_ColorUp.PrimaryColor = RGB(0, 255, 0);
        Subgraph_ColorUp.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_ColorUp.LineWidth = 1;
        Subgraph_ColorUp.DrawZeros = false;

        Subgraph_ColorDown.Name = "Bar Color Down";
        Subgraph_ColorDown.PrimaryColor = RGB(255, 0, 0);
        Subgraph_ColorDown.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_ColorDown.LineWidth = 1;
        Subgraph_ColorDown.DrawZeros = false;

        Subgraph_VolImbUp.Name = "Volume Imbalance Up";
        Subgraph_VolImbUp.PrimaryColor = RGB(255, 255, 255);
        Subgraph_VolImbUp.DrawStyle = DRAWSTYLE_POINT;
        Subgraph_VolImbUp.LineWidth = 1;
        Subgraph_VolImbUp.DrawZeros = false;

        Subgraph_VolImbDown.Name = "Volume Imbalance Down";
        Subgraph_VolImbDown.PrimaryColor = RGB(255, 255, 255);
        Subgraph_VolImbDown.DrawStyle = DRAWSTYLE_POINT;
        Subgraph_VolImbDown.LineWidth = 1;
        Subgraph_VolImbDown.DrawZeros = false;

        Input_WebhookURL.Name = "Discord Webhook URL";
        Input_WebhookURL.SetString("");
        Input_WebhookURL.SetDescription("Empty = disabled. Paste a webhook URL to send BUY/SELL alerts (stored in chartbook - keep chartbooks private).");

        Input_RotationMinTicks.Name = "Rotation Min Ticks (0 = filter off)";
        Input_RotationMinTicks.SetInt(0);
        Input_RotationMinTicks.SetIntLimits(0, 500);

        Input_MinRotDensity.Name = "Min Rotation Density (0 = off)";
        Input_MinRotDensity.SetFloat(0.0f);
        Input_MinRotDensity.SetFloatLimits(0.0f, 1000000000.0f);

        Subgraph_RotDelta.Name = "Rotation Delta (hidden)";
        Subgraph_RotDelta.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_RotDelta.DrawZeros = false;

        Subgraph_RotDensity.Name = "Rotation Density (hidden)";
        Subgraph_RotDensity.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_RotDensity.DrawZeros = false;
        return;
    }

#pragma endregion

    int i = sc.Index;
    int &RotSide = sc.GetPersistentInt(0);
    double &RotHigh = sc.GetPersistentDouble(1);
    double &RotLow = sc.GetPersistentDouble(2);
    double &RotDelta = sc.GetPersistentDouble(3);
    double &RotVol = sc.GetPersistentDouble(4);
    double &RotSplit = sc.GetPersistentDouble(5);
    if (i == 0)
    {
        // Full recalculations replay from bar 0 with stale persistents.
        RotSide = 0;
        RotHigh = 0.0;
        RotLow = 0.0;
        RotDelta = 0.0;
        RotVol = 0.0;
        RotSplit = 0.0;
    }
    Subgraph_VolImbUp[i] = 0;
    Subgraph_VolImbDown[i] = 0;
    Subgraph_RotDelta[i] = 0.0f;
    Subgraph_RotDensity[i] = 0.0f;
    if (i < 2)
        return;
    int BarCloseStatus = sc.GetBarHasClosedStatus() == BHCS_BAR_HAS_CLOSED;
    SCBaseDataRef in = sc.BaseData;
    double close = sc.Close[i];
    double open = sc.Open[i];
    double high = sc.High[i];
    double low = sc.Low[i];
    double pclose = sc.Close[i - 1];
    double popen = sc.Open[i - 1];
    double phigh = sc.High[i - 1];
    double plow = sc.Low[i - 1];
    double body = fabs(open - close);
    double pbody = fabs(popen - pclose);
    bool red = open > close;
    bool green = open < close;
    bool pdoji = false;
    double upperwick = 0;
    double lowerwick = 0;

    SCFloatArrayRef Array_Value = Subgraph_Calc.Arrays[0];

    const SCString WebhookURL = Input_WebhookURL.GetString();
    // Rotation/density filter (toobrien/acsil adaptation to bars): signed
    // per-bar delta accumulates into the current rotation; a close that
    // travels MinRotTicks from the rotation extreme flips the side and
    // restarts the accumulators. Density = rotation volume per tick of
    // rotation range (absorption proxy). Requires bid/ask volume (Numbers
    // bars): with no split volume in the rotation both sides are blocked,
    // so a missing feed degrades to no signals, never a long bias. With
    // MinRotTicks = 0 the block is skipped and signals are bit-for-bit
    // legacy. State updates on closed bars only (no repaint).
    const int MinRotTicks = Input_RotationMinTicks.GetInt();
    const float MinRotDensity = Input_MinRotDensity.GetFloat();
    bool rotOkLong = true;
    bool rotOkShort = true;
    if (MinRotTicks > 0 && BarCloseStatus)
    {
        const double tick = (sc.TickSize > 0.0) ? sc.TickSize : 1.0;
        const double d = (double)sc.AskVolume[i] - (double)sc.BidVolume[i];
        const double v = (double)sc.Volume[i];
        const double split = (double)sc.AskVolume[i] + (double)sc.BidVolume[i];
        if (RotSide == 0)
        {
            RotSide = (d >= 0.0) ? 1 : -1;
            RotHigh = high;
            RotLow = low;
            RotDelta = d;
            RotVol = v;
            RotSplit = split;
        }
        else
        {
            RotDelta += d;
            RotVol += v;
            RotSplit += split;
            if (high > RotHigh)
                RotHigh = high;
            if (low < RotLow)
                RotLow = low;
            const double upTicks = (close - RotLow) / tick;
            const double dnTicks = (RotHigh - close) / tick;
            if (RotSide <= 0 && upTicks >= (double)MinRotTicks)
            {
                RotSide = 1;
                RotHigh = close;
                RotLow = close;
                RotDelta = d;
                RotVol = v;
                RotSplit = split;
            }
            else if (RotSide >= 0 && dnTicks >= (double)MinRotTicks)
            {
                RotSide = -1;
                RotHigh = close;
                RotLow = close;
                RotDelta = d;
                RotVol = v;
                RotSplit = split;
            }
        }
        const double rangeTicks = (RotHigh - RotLow) / tick;
        const double density = (rangeTicks >= 1.0) ? RotVol / rangeTicks : RotVol;
        Subgraph_RotDelta[i] = (float)RotDelta;
        Subgraph_RotDensity[i] = (float)density;
        rotOkLong = (RotSplit > 0.0 && RotSide > 0 && density >= (double)MinRotDensity);
        rotOkShort = (RotSplit > 0.0 && RotSide < 0 && density >= (double)MinRotDensity);
    }
    else
    {
        Subgraph_RotDelta[i] = 0.0f;
        Subgraph_RotDensity[i] = 0.0f;
    }

    if (BarCloseStatus && rotOkLong && IsVolImbGreen(sc, i))
    {
        sc.AddLineUntilFutureIntersection(i, i, open, RGB(255, 255, 255), 2, LINESTYLE_SOLID, false, false, "");
        Subgraph_VolImbUp[i] = low - (2 * sc.TickSize);
        txt.Format("Volume Imbalance BUY at %.2f", close);
        if (sc.IsNewBar(i))
        {
            sc.AlertWithMessage(181, "Volume Imbalance BUY");
            MaybeSendWebhook(sc, WebhookURL, txt);
        }
    }

    if (BarCloseStatus && rotOkShort && IsVolImbRed(sc, i))
    {
        sc.AddLineUntilFutureIntersection(i, i, open, RGB(255, 255, 255), 2, LINESTYLE_SOLID, false, false, "");
        Subgraph_VolImbDown[i] = high + (2 * sc.TickSize);
        txt.Format("Volume Imbalance SELL at %.2f", close);
        if (sc.IsNewBar(i))
        {
            sc.AlertWithMessage(182, "Volume Imbalance SELL");
            MaybeSendWebhook(sc, WebhookURL, txt);
        }
    }
}