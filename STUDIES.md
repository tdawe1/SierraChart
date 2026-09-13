# STUDIES.md — Custom Study Index (agent-facing)

Single location for every Sierra Chart custom study available to this workspace.
Root sources live in this repo (`*.cpp`, `EdgeFul Indicators/`, `zbyte/`).
External sources are reference copies under `studies/vendor/` (see
`studies/SOURCES.md` for origins and sync policy). Human-browsable catalog:
`studies/index.html`.

Build: `python3 tools/sc.py build dll && python3 tools/sc.py build verify`
for local MinGW `_64.dll`s straight into `Data/` (restart Sierra after —
it scans `Data/` once at startup), or copy the file(s) into `ACS_Source`
and Remote Build (`build stage` flattens sources+headers for a one-trip
server build; see Agent build notes).
Unless noted, each file is a standalone translation unit.
Known build conflicts are flagged with ⚠ below — do not compile both
files into the same DLL/in the same build folder selection.

Status values: **Active** (built `_64.dll` committed in this repo) ·
**Source-only** (no DLL committed) · **Legacy** (superseded version) ·
**Variant** (one of several same-named alternatives — pick one per build) ·
**Helper/Test** (not a chart study) · **Binary-only** (DLL, source unknown) ·
**Reference** (vendor copy; canonical home is another repo).

## 1. Root — house studies (`tdawe1/SierraChart`, upstream `TraderOracle/SierraChart`)

| File | Study (`scsf_`) / chart name | What it does | Status |
|---|---|---|---|
| `TraderOracle.cpp` | `Olympus` / Olympus | Flagship signal study; configs in `TO Studies.StdyCollct` | Active (`TraderOracle_64.dll`) |
| `TraderOracle.cpp` | `OlympusOLD` / Olympus OLD | Previous Olympus generation | Legacy |
| `TraderOracle.cpp` | `Delta_Intensity` / Delta Intensity | Delta intensity panel ⚠ same export as `Renko_GOAT.cpp` — never build together | Active |
| `TraderOracle.cpp` | `SierraSqueeze` / Squeeze Indicator 2 | Squeeze indicator (by Tony C.) | Source-only |
| `TraderOracle.cpp` | `LindaMACD` / Linda MACD | Linda Raschke MACD variant | Source-only |
| `TraderOracle.cpp` | `WaddahExplosion` / Waddah Explosion | Waddah Attar Explosion port | Source-only |
| `TraderOracle.cpp` | `Linda_Anti_Setup` / Linda Raschke Anti Setup | Anti-setup pattern | Source-only |
| `TraderOracle.cpp` | `DTS_Scalper` / DTS Scalper | Scalper signals | Source-only |
| `TraderOracle.cpp` | `GraphicsSettingsExample`, `test` | Sierra example / marker scratch | Helper/Test |
| `Renko_GOAT.cpp` | `RabbitWatcher` / Rabbit Watcher | MACD-cross vs EMA hop signals, color bars | Source-only |
| `Renko_GOAT.cpp` | `RenkoGOAT` / Renko GOAT | Renko companion signals | Source-only |
| `Renko_GOAT.cpp` | `Delta_Intensity` / Delta Intensity | ⚠ duplicate export — see `TraderOracle.cpp` | Source-only |
| `TORobots.cpp` | `GoldBug` / GoldBug | Auto-trader: direction filter, buy/sell, imbalance, engulfing-off-BB, FVG, max positions/loss/profit; order sending defaults OFF, max-loss/profit halt enforced (safety pass 2026-09-12) | Active (`TORobots_64.dll`) |
| `godtrades.cpp` | `GodTrades` / God Trades | Multi-filter signals: Waddah, MACD, SAR, Supertrend, AO, Fisher, HMA, T3, ADX floor, doji handling, bar-color modes | Source-only |
| `VolImbRenko.cpp` | `VolImbRenko` / VolImb RENKO | Volume-imbalance gap detector (open gaps beyond prior close) + optional Discord webhook (empty URL = disabled); future-bar lookahead removed 2026-09-12 | Source-only |
| `MarketCipherWannabe.cpp` | `MarketCipherWannabe` | Extended edition of the VMC Cipher B divergences: same core + per-category alerts (small/big/div/gold/MFI, once-per-bar latches, bar-close option) mirrored to hidden flag subgraphs | Source-only |
| `vumanchu1.cpp` | `VuManChuCipherBDivergences` / VMC Cipher_B_Divergences | Base edition: VuManChu Cipher B divergences port, minimal Buy/Sell/Gold alerts | Source-only |
| `mancini.cpp` | `Mancini_Lines` / Mancini Lines | Support/resistance + major lines, text label, recalc interval | Source-only |
| `ManciniPlusConverter.cpp` | `ManciniPlus` / Mancini Plus | Newer sibling: S/R + majors, ratio scanner input, label, recalc interval | Source-only |
| `Killpips.cpp` | `Killpips_Levels` / Killpips Levels v1.4 | Color-coded horizontals (VIX/VAL/VAH/MAX/MIN) parsed from a `desc: price,…` string | Active (`Killpips_64.dll`) |
| `MarketMaker.cpp` | `MoneyMaker_Levels` / Market Maker Levels | L1–L5/H1–H5/Mid horizontals parsed from a `desc,price` string; per-day drawing namespaces (multi-day clobber fixed 2026-09-12) | Source-only |
| `LRS.cpp` | `LinearRegSlopeWithColor` / DaveC's LRS | Linear-regression slope histogram + EMA smoothing, threshold colors, background flash on cross | Active (`LRS_64.dll`) |
| `StreamSounds.cpp` | `StreamSounds` / Stream Sounds | Scheduled session sound alerts (NY open, pivots, auctions…) + file-trigger sounds, on-chart text | Source-only |
| `FancyNews.cpp` | `FancyNews` / Fancy News | File-driven news-line overlay (`C:\temp\today.txt`); uses alert 29 | Source-only |
| `TraderSmarts_Unofficial.cpp` | `TraderSmarts` / TraderSmarts Unofficial | Stored price-level touch/wick alerts (alerts 26/27) | Source-only |
| `MeanReversionOU.cpp` | `MeanReversionOU` / Mean Reversion OU | OU/z-score mean reversion with half-life time-stop and stdev-floor gates. Signal-only. | Source-only |
| `StatArbPairs.cpp` | `StatArbPairs` / Stat Arb Pairs | Pairs spread mean reversion: rolling OLS hedge, z-scored spread, correlation + half-life gates. Signal-only. | Source-only |
| `GoldenCrossRegime.cpp` | `GoldenCrossRegime` / Golden Cross Regime | Long-only SMA golden-cross with extension guard and index bull-regime filter, death-cross exits, 1-ROC score. Signal-only. | Source-only |
| `MondayDipBuy.cpp` | `MondayDipBuy` / Monday Dip Buy | Long-only Monday dip-buy above rising slow SMA, bounce-or-time exits, 1-ROC score. Signal-only. | Source-only |
| `SignalExecutor.cpp` | `SignalExecutor` / Signal Executor | Indicator-triggered ACSIL executor: closed-bar trigger subgraphs to market entries, opposite-exit, attached stop/target; sim default. | Active |
| `ORBRetrace.cpp` | `ORBRetrace` / ORB Retrace (NQ) | Intraday OR breakout-retrace: close-confirmed break, VP-level retrace entry, OR-extreme stop, measured-move target, EOD flat. Signal-only. | Source-only |
| `BacktestHarness.cpp` | `BacktestHarness` / Backtest Harness | File-driven study deployer: ADD/SET/WIRE/VERIFY/RECALC/REMOVE studies on its chart from a job file; zero GUI per study after bootstrap. | Active |

## 2. `EdgeFul Indicators/` — session-level toolkit (all source-only, no DLLs)

| File | Study / chart name | What it does |
|---|---|---|
| `DayOfWeekBias.cpp` | `DayOfWeekBias` / Day Of Week Bias | Historical green/red weekday readout, colored by dominant side |
| `FirstHourTrend.cpp` | `FirstHourTrend` / NY First Hour Trend | First-hour trend stats on intraday (Central TZ) |
| `FourLineChartText.cpp` | `FourLineChartText` / Four Line Chart Text | 4 pinned text lines, fixed LineNumbers, replace-via-`UTAM_ADD_OR_ADJUST` |
| `InsideBarScanner.cpp` | `InsideBarScanner` / Inside Bar Scanner (20 Symbols) | Scans ≤20 daily charts in book for last completed inside bar |
| `InsideBarScannerBlink.cpp` | `InsideBarScanner` (same export) | ⚠ Variant with blinking display — pick one per build |
| `MidnightLevel.cpp` | `MidnightLevel` / Midnight Level | Midnight-open level on intraday (Central TZ) |
| `OneHourOR_HighLow_First.cpp` | `OpeningRangeSequence` / 1H Opening Range Sequence | When the 1H range high/low printed first |
| `PrevSessionCloseTracker.cpp` | `PrevSessionCloseTracker` | Prior 5pm futures close tracker |
| `SundayOpenLevel.cpp` | `SundayOpenLevel` / Sunday Open Level | 17:00 Sunday reopen level |
| `TimeSlotValue.cpp` | `TimeSlotValue` / Time Slot Value | CSV table (`ES/NQ/YM_FiveMinStats.csv`) rendered as color-coded rectangles |

## 3. `zbyte/` — ported indicators (all source-only except TrendArchitect)

| File | Study / chart name | What it does | Status |
|---|---|---|---|
| `TrendArchitect.cpp` | `TrendArchitect` / Trend Architect v9.3.1 | Trend system; **requires Advanced subscription** | Active (`zbyte/TrendArchitect_64.dll`) |
| `SCOFA-v1203.cpp` | `SCOFAbsorptionDetector` | Orderflow absorption from VAP, 3 patterns | Legacy |
| `SCOFA-v1205.cpp` | `SCOFAbsorptionDetector` | Same export, 4 patterns | Legacy |
| `SCOFA-v1206.cpp` | `SCOFAbsorptionDetector` | Same export, 4 patterns — current of the three | Source-only |
| `SqueezeChannel.cpp` | `SqueezeChannel` / Squeeze Channel | Pine v6 "Squeeze Channel" port, v1.1.1 | Source-only |
| `TLADe_GEX_Levels.cpp` | `TLADe_GEX_Levels` / TLADe GEX Levels + BOS + Session AVWAP | NQ/ES gamma Pine ports merged, single study | Source-only |
| `EhlersDominantStoch.cpp` | `EhlersDominantCycleStochRSI` | Ehlers Dominant Cycle Stochastic RSI, Pine v6 1:1 port | Source-only |
| `c-zchann.cpp` | `RollingZScoreChannel` | Adaptive rolling z-score channel Pine port | Variant (same export as `g-zchann`) |
| `g-zchann.cpp` | `RollingZScoreChannel` | Same study, alternate port — pick one per build | Variant |

## 4. `studies/vendor/sierrachart-studies/` — Orion family (Reference copies)

Canonical home: `tdawe1/SierraChartStudies` (fork of `TradesTrevor/SierraChartStudies`).
`Orion.cpp` was removed from this repo root by commit `4f3acb0`; do not re-add it —
edit upstream and re-sync the vendor copy.

| File | Study | What it does |
|---|---|---|
| `Orion.cpp` | `OrionAbsorptionClimax`, `OrionAccountBalance` | Absorption-climax setups + configurable alerts + data overlay + live (non-reconciled) account balance. Self-contained single file for Remote Build |
| `orion_core.h` | — | Testable pure-logic mirror of Orion; `orion_core_test.cpp` upstream runs with `g++ -std=c++17` |
| `PropRiskOverlay.cpp` | `PropRiskOverlay` | Live prop guardrails (daily loss cap, drawdown halt) drawn on chart |
| `FlipperStudies.cpp` | `TheFlipper`, `DeltaColoredCandles`, `DynamicFlipper` | Flipper signals + delta-colored candles |
| `SatyPivotRibbon.cpp` | `SatyPivotRibbon` | 3-EMA ribbon trend/support-resistance |
| `DiscordAlerts.cpp` | `DiscordTradeAlert` | Trade webhook alerts to Discord |
| `InitialBalanceStatistics.cpp` | `InitialBalanceStatistics` | Initial-balance statistics |
| `BacktestExporter.cpp` | `BacktestExporter` | Bar+signal CSV dump for the headless backtester — tooling, not a chart study |
| `AllStudies.cpp` (not copied — regenerate upstream with `python3 bundle.py`) | all of the above in one TU | Generated single-DLL bundle of the Studies repo; stale the moment sources change, so always regenerate, never archive |

## 5. `studies/vendor/frozentundra/` — DOM/tape/VP utilities (Reference copies)

Canonical home: `FrozenTundraTrader/sierrachart`. `helpers.h` ships with them
(`JIGSAW_Export.cpp` needs it). GDI-drawn studies need OpenGL **off**.

| File | Study | What it does |
|---|---|---|
| `TapeOnChart.cpp` | `TapeOnChart` | On-chart time & sales tape, large/huge prints, iceberg detection, pinned prints (GDI) |
| `magic_charts.cpp` | `Magic` | Tick-execution dot-matrix renderer, bid/ask chars, large-print marks |
| `pace_of_tape.cpp` | `PaceOfTape` | Ticks/sec or volume/sec gauge, squares/circles, slow→fast gradient |
| `market_depth_sizes.cpp` | `MarketDepthSizes` | DOM depth size labels with min-size filter (needs market-depth data) |
| `avg_lot_size.cpp` | `AverageLotSize` | Avg lots per order in DOM General Purpose Column 1 (needs market-depth data) |
| `price_in_label.cpp` | `PriceInLabel` | DOM price in Label/GP column, last-trade highlight |
| `number_highs_lows.cpp` | `NumHighsLows` | Session high/low counts overlay (GDI) |
| `google_sheets_importer.cpp` | `GoogleSheetsLevelsImporter` | Levels from a shared Google Sheet CSV, drawn on chart and/or DOM |
| `JIGSAW_Export.cpp` | `JigsawExport` | Level export (ovnH/L, VWAPs, deviations, EQs) to CSV for Jigsaw |
| `auto_risk_reward.cpp` | `AutoRiskReward` | Risk/reward drawing from the open position (AutoRR) |
| `auto_bar_period.cpp` | `AutoBarPeriod` | Source/target bar-period sync across charts |
| `auto_numbars_volmult.cpp` | `AutoNumBarsVolMult` | Auto NumBars/NumBarsCalc from volume-multiplier studies |
| `auto_vbp_fixes.cpp` | `AutoVbP` | Auto Volume-by-Price ticks-per-bar per symbol/visible range |
| `zoom_toggle.cpp` | `ZoomToggle` | Keyboard zoom toggle (default Space) to N bars |
| `vap_mult.cpp` | `ChangeVolAtPriceMult` | Auto Volume-At-Price multiplier from visible bar ranges |
| `helpers.h` | — | Shared helper header | Helper/Test |

## 6. `studies/vendor/jm-jo-nq100/` — French NQ100 toolkit (Reference copies)

Canonical home: `JM-JO/Sierra-Chart---DLLs` (`src/`). French-language,
NQ100 futures/index focus. All source-only.

| File | Studies | What it does |
|---|---|---|
| `Niveaux_psy_Fut_NQ100_v2.1.cpp` | `Niveaux` | Psychological levels, vector-drawn, futures |
| `Niveaux_psy_Ind_NQ100_v3.5.cpp` | `NiveauxINQ100`, `NiveauxINQ100Projetes` | Index psych levels on index + projected onto futures |
| `Overlay_INQ100_v2.5.cpp` | `OverlayINQ100PlusSpread` | Index overlay on futures with spread correction |
| `Points_pivots_Fut_NQ100_v2.3.cpp` | `MesPointsPivots*` (5) | Futures pivot ladders M/H/J/4H/1H, Full/Mid/Quart |
| `Points_pivots_Ind_NQ100_v6.0.cpp` | `MesPointsPivots*`, `MesEighths*` (8) | Index pivot ladders + eighth/sixteenth splits, projected variants |
| `PH_PB_Dyn_v1.7.5.cpp` | `PHPBDynamiques*`, `DistanceAPHPB*` (6) | Dynamic previous-high/low + distances, futures/index/SP500 |
| `VWAPs_future_NQ100_v1.7.cpp` | `VWAPsCalcul`, `VWAPsAffichage` | J/H/M VWAP calculation + display pair |
| `SpreadIndex_NQ_v1.7.cpp` | `SpreadIndex*` (6) | Spread-index calc/display, session averages, price distribution |
| `Spread_Future_Index_NQ_v1.7.cpp` | `SpreadFutureIndex*` (5) | Future-index spread for subchart display |
| `Suivi_tendance_spread_future_indice_v5.3.cpp` | `StudyPrincipale`, `AffichageGainCumule*`, `TestEcritureFichierCSV`, `VariationParametresTestes` | Spread trend-follow system + gain display + CSV/param-test scaffolding |
| `Prix_typique_Tour_De_Controle_v5.2.cpp` | `PrixTypique`, `VWAPsAffichage`, `PHPBDynamiquesForFuture` | Typical price / control tower |
| `Range_TrueRange_v2.5.cpp` | `Range`, `SuperTrueRange`, `Sigma*` (8) | Range/TR family incl. sigma-scaled variants |
| `Sigma_v5.2.cpp` | `Sigma*`, `AnnulationMiniRebond` (7) | Sigma ladders x512→x0.25 + mini-rebond |
| `Quantieme_mvt_v6.8.cpp` | `QuantiemeMvt` | Date-fraction move analysis |
| `Latences_v4.8.cpp` | `Latency*`, `DureeCycleChart*`, `DebugCompteurMessageLog` (6) | Data-feed latency + chart-cycle diagnostics |
| `Ligne_verticale_horaire_v2.0.0.cpp` | `LigneVerticale*`, `PackageHoraires*`, `ZoneVerticale` (8) | Hourly vertical lines/zones + session time packages |
| `Navig_niveaux_v2.3.cpp` | `NavigationNiveaux` | Level navigation |
| `Padding_et_Boutons_ACS_v2.9.cpp` | `Padding_Et_BoutonsACS` | Chart padding + ACS buttons |
| `Annul_mini_rebond_v9.3.2.cpp` | `AnnulationMiniRebond` | Mini-pullback invalidation levels |

## 7. Binary-only DLLs (no source in this repo — do not treat as buildable)

`ATRPositionSizing`, `BBDynamicSR`, `CumulativeDeltaDivDetector`, `DATR`,
`FVG_and_News`, `IVRVRatio`, `IVRankPercentile`, `LinearRegression`,
`MomoVolatility`, `RankCorrelationIndex`, `RiskRewardTool`, `SmashDay`,
`TTMSqueeze`, `VIXSmartLevels`, `gcUserStudies_DOMNotes_64.dll`,
`gcUserStudies_FlowGauges_64.dll`, `gcUserStudies_MomentumTails_64.dll`
(`DOMNotes`, `FlowGauges`, `MomentumTails` studies), `reclaims`, `ColorThemeSwitcher` (from `~/Downloads` — two bit-identical copies, one kept). Source unknown — keep as binaries; if a source
surfaces, add it to the tables above and flip the status.

## 8. Chart presets (not studies, but wired to studies above)

`TO Studies.StdyCollct` (Olympus configs), `Roboto.StdyCollct`,
`Roboto Backtest.StdyCollct`, `Professor Lines.StdyCollct`,
`Footprint Deluxe.StdyCollct`, `ORB.StdyCollct`, `TO Footprint.Cht`,
`TO Main.Cht`, `ORB.Cht`, `Delta Heatmap.Cht`, `Piper_TPO.Cht`,
`Professor RV Lines.Cht`, `CSRobot.Cht`,
`SC Chartbook E-MINI SP500 SierraEdge.Cht`,
`SC Chartbook E-MINI NASDAQ-100 SierraEdge.Cht` (×2; `.bak-*` copies are
local backups — keep them out of the repo).

## Alert ID registry (chart-global — never reuse an ID across studies)
|`check` flags any ID shared by two files. Within `TraderOracle.cpp` the two
Olympus generations also use distinct IDs. 191–192 are free (TORobots GoldBug
VolImb/FVG 197/198 sit in a commented-out block — do not reactivate them
without renumbering). 197–200 no longer collide: 199/200 belong to
`StatArbPairs.cpp`, 201–204 to `GoldenCrossRegime.cpp` / `MondayDipBuy.cpp`.
| ID(s) | Owner | Signal |
|---|---|---|
| 5–8, 12–13, 17–24 | `godtrades.cpp` | God Trades signals (5/6 = BUY/SELL, 7/8 on NQ symbols) |
| 26, 27 | `TraderSmarts_Unofficial.cpp` | Stored-level touch / wick |
| 29 | `FancyNews.cpp` | 2-minute news warning |
| 161–169 | `zbyte/SqueezeChannel.cpp` | Squeeze breakouts/reversals (moved off 1–9, which hit GodTrades) |
| 171–176 | `zbyte/c-zchann.cpp` + `g-zchann.cpp` | Channel break/re-entry/basis-cross (pick-one pair shares; moved off 1–6, which hit GodTrades) |
| 181, 182 | `VolImbRenko.cpp` | Volume Imbalance BUY / SELL |
| 183, 184 | `TraderOracle.cpp` Olympus | BUY / SELL |
| 185, 186 | `TraderOracle.cpp` Olympus VolImb legs | BUY / SELL |
| 187, 188 | `TraderOracle.cpp` Olympus OLD | BUY / SELL |
| 189, 190 | `TraderOracle.cpp` Olympus OLD VolImb legs | BUY / SELL |
| 191, 192 | `MeanReversionOU.cpp` | Mean-reversion BUY / SELL |
| 193, 194 | `Renko_GOAT.cpp` | Renko GOAT BUY / SELL |
| 195, 196 | `TORobots.cpp` GoldBug | Standard BUY / SELL (197/198 VolImb/FVG block is commented out) |
| 199, 200 | `StatArbPairs.cpp` | Spread BUY / SELL |
| 201, 202 | `GoldenCrossRegime.cpp` | Golden-cross BUY / death-cross EXIT |
| 203, 204 | `MondayDipBuy.cpp` | Dip-buy BUY / bounce-or-time EXIT |
| 205, 206 | `ORBRetrace.cpp` | ORB entry / exit (Buy leg = long entries + short exits) |

- Tooling: `tools/sc.py`
  (`new`/`catalog`/`install`/`check`/`maintain`/`sync`/`data`/`strategies`/`optimize`/`confirm`/`backtest`/`build`)
  + `tools/README.md`. Run `python3 tools/sc.py check` before any Remote Build;
  deploy with `install`, backtests delegate to the upstream engine
  (`~/SierraChartStudies/backtest/`), never a local copy.
- Compiling (two methods — local first, server when you must):
  `build plan` (dep closure, fails on missing headers) →
  `build local` (MinGW syntax check, seconds) →
  `build dll [--to DIR] [--force]` (links UCRT, drops `*_64.dll` into
  `Data/`, no server round-trip; restart Sierra, it scans `Data/` once
  at startup) → `build verify` (each DLL fresh and exporting its
  `scsf_` names). Prefer this loop for iteration.
  Remote Build path instead: `build stage [--dry-run]` flattens sources +
  companion headers (`zbyte/include/`, `zbyte/modules/`) into `ACS_Source`
  so quoted includes resolve on the server too, then Analysis → Build
  Custom Studies DLL selecting every staged `.cpp` in ONE build (one
  trip, one `_64.dll` per study). `install` uses the same layout.

- Remote Build compiles the selected `ACS_Source` file(s) into one DLL per
  build; study names on-chart come from `sc.GraphName`, not filenames.
- `SCSFExport` is `extern "C"`: never merge two study files with same-named
  `scsf_` into one translation unit (known collisions: `Delta_Intensity`,
  `InsideBarScanner`, `RollingZScoreChannel`, `SCOFAbsorptionDetector`).
- `DiscordAlerts.cpp` lesson: pass `SCString` to `const char*` params via
  `.GetChars()`, never implicit conversion. `SatyPivotRibbon.cpp` lesson:
  `SCStudyInterfaceRef` (not `SCStudyGraphRef`), `DRAWSTYLE_COLOR_BAR`
  (not `DRAWSTYLE_COLORBAR`) — verify names against Sierra docs before
  touching source.
- Syntax check with `build local` (uses the real `sierrachart.h` from
  `$SC_ACS_SOURCE` / Wine `ACS_Source`); the `/tmp/orion_check` stub is
  fallback only, and must be refreshed when new ACSIL APIs are used.
- `*_64.dll` are committed build artifacts in this repo (kept deliberately for
  one-click install); never commit new ones without need.
