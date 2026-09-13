# DLL provenance (generated 2026-09-13 by binary audit of Data/*_64.dll)
# Covers remote-build outputs and vendor drops. Regenerate after any rebuild:
# sha256sum per DLL + objdump import/export scan (see PR review notes).
# Result: no DLL imports network APIs (WINHTTP/WININET/URLMON/WS2_32).
# DiscordAlerts carries no embedded webhook (URL is a runtime input; errors when unset).

| DLL | SHA-256 (12) | studies | network |
|---|---|---|---|
| ALERT_LOG_MONITOR_64.dll | `b4cb3f6f035e` | 1 | none |
| AllStudies_64.dll | `60ff65d8ff52` | 9 | none |
| BacktestExporter_64.dll | `d3b01137aec3` | 1 | none |
| BacktestHarness_64.dll | `6e6d479a3151` | 1 | none |
| CHART_NAVIGATOR_64.dll | `34f9b81fc761` | 1 | none |
| CVD_FILLED_AREA_64.dll | `b48eb6660fd8` | 1 | none |
| ColorThemeSwitcher_64.dll | `02c238df45a4` | 1 | none |
| DayOfWeekBias_64.dll | `b0391fdae6c6` | 1 | none |
| DiscordAlerts_64.dll | `c45ef80b0263` | 1 | none |
| EhlersDominantStoch_64.dll | `376f800146be` | 1 | none |
| FancyNews_64.dll | `f0bddb556735` | 1 | none |
| FirstHourTrend_64.dll | `cacdb7d8020a` | 1 | none |
| FourLineChartText_64.dll | `af57114690da` | 1 | none |
| GoldenCrossRegime_64.dll | `13f2da59bbf7` | 1 | none |
| InitialBalanceStatistics_64.dll | `dd810850b321` | 1 | none |
| InsideBarScannerBlink_64.dll | `f82cb4581012` | 1 | none |
| InsideBarScanner_64.dll | `690f65eedcf1` | 1 | none |
| Killpips_64.dll | `76ae0c1617f4` | 1 | none |
| LRS_64.dll | `fda315fae716` | 1 | none |
| LumiraOriaLabs_64.dll | `7472edecdbd2` | 1 | none |
| MARKET_DEPTH_MANAGER_64.dll | `d084cf2587cb` | 1 | none |
| ManciniPlusConverter_64.dll | `8c66923fb6c3` | 1 | none |
| MarketCipherWannabe_64.dll | `4648aa1d933a` | 1 | none |
| MarketMaker_64.dll | `7bfb0e56656e` | 1 | none |
| MeanReversionOU_64.dll | `dacb44584d04` | 1 | none |
| MidnightLevel_64.dll | `7e4c52623efa` | 1 | none |
| MondayDipBuy_64.dll | `ee4d38e8897b` | 1 | none |
| ORBRetrace_64.dll | `390c7a920ce5` | 1 | none |
| OneHourOR_HighLow_First_64.dll | `635222686ba4` | 1 | none |
| Orion_64.dll | `12453ba44d5d` | 1 | none |
| PrevSessionCloseTracker_64.dll | `336a641a2e84` | 1 | none |
| ROUND_PRICE_LEVELS_64.dll | `643645985eef` | 1 | none |
| Renko_GOAT_64.dll | `96a86f61c709` | 3 | none |
| SCOFA-v1203_64.dll | `87cc2d4315f7` | 1 | none |
| SCOFA-v1205_64.dll | `989cb59367f8` | 1 | none |
| SCOFA-v1206_64.dll | `a8dda502de9c` | 1 | none |
| SESSION_ATR_PERCENT_64.dll | `5dd567c3baf8` | 1 | none |
| SINGLE_PRINT_AND_GAP_WITHOUT_TPO_64.dll | `25d2e118e2d1` | 1 | none |
| SatyPivotRibbon_64.dll | `496da94d9165` | 1 | none |
| SignalExecutor_64.dll | `c7e18cd53355` | 1 | none |
| SqueezeChannel_64.dll | `a258ae0a2819` | 1 | none |
| StatArbPairs_64.dll | `7aa9f7fe3247` | 1 | none |
| StreamSounds_64.dll | `bac190e01c76` | 1 | none |
| SundayOpenLevel_64.dll | `e5120cb102d0` | 1 | none |
| SwingCalls_64.dll | `a78c4da1c9cd` | 1 | none |
| TLADe_GEX_Levels_64.dll | `883898e6e4bd` | 1 | none |
| TORobots_64.dll | `0cc2801ade15` | 1 | none |
| TRADE_MANAGER_64.dll | `6a9da194c153` | 1 | none |
| TRADING_JOURNAL_64.dll | `bd0d8405b5cb` | 1 | none |
| TheFlipper_64.dll | `3bbff3d14f6f` | 2 | none |
| TimeSlotValue_64.dll | `c057e410fd7d` | 1 | none |
| TraderOracle_64.dll | `edd1a2c09c81` | 8 | none |
| TraderSmarts_Unofficial_64.dll | `1e93e1791837` | 1 | none |
| TrendArchitect_64.dll | `75e54972aef1` | 1 | none |
| UserContributedStudies_64.dll | `96e70f4850e9` | 182 | none |
| VolImbRenko_64.dll | `e9b90bf273df` | 1 | none |
| ZERO_PRINT_ZONES_64.dll | `c6e39f47c586` | 1 | none |
| c-zchann_64.dll | `830c3fccd5ad` | 1 | none |
| g-zchann_64.dll | `a44bb6035d26` | 1 | none |
| gcUserStudies_DOMNotes_64.dll | `09e28404784d` | 1 | none |
| gcUserStudies_FlowGauges_64.dll | `1ab37452b8cb` | 7 | none |
| gcUserStudies_Free_64.dll | `1e236dbfa967` | 8 | none |
| gcUserStudies_MomentumTails_64.dll | `b49dabb1ea91` | 2 | none |
| godtrades_64.dll | `069fc271fc75` | 1 | none |
| mancini_64.dll | `211a6efd3a54` | 1 | none |
| vumanchu1_64.dll | `c2958f84ef26` | 1 | none |
