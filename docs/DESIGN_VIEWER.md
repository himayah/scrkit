# SCRAPI 対応 spiral-suction-saver 拡張と ScrViewer の設計 -- ドラフト

> 状態: **設計ドラフト + 初期実装** (ブランチ `feature/scrapi-viewer`)。実装の進み具合と設計からの変更点は §F。
> 汎用 API 仕様は [`SCRAPI_SPEC.md`](SCRAPI_SPEC.md)。本書は (A) その仕様の採用方針、
> (B) spiral-suction-saver 側の拡張、(C) ビュワー `ScrViewer`、(D) 検証計画・段階計画、
> (E) 未決事項、をまとめる。本書は設計を固める段階のもので、現行仕様は [`DESIGN.md`](DESIGN.md)。

## 0. 要件の整理

| 要望 | 設計上の帰結 |
|---|---|
| `.scr` はスクリーンセーバーとして機能し続ける | `/s` `/c` `/p` は不変。SCRAPI は追加スイッチ `/scrapi` が付いた `/p` でのみ有効 (§A.2) |
| 内部機能を個別に呼び出せる API | セーバーが**マニフェスト**でコントロールを宣言し、**JSON Lines** で get/set/invoke を受ける (SPEC §4-5) |
| 専用ビュワーが `.scr` を読み込みウィンドウ内にプレビュー | 標準の `/p <hwnd>` を使い、ビュワー内の子ウィンドウとして描画させる (§C.2) |
| API のリスト取得→機能に応じたコントローラを表示 | マニフェストから UI を自動生成 (§C.3)。ノブ・スライダ・ラジオ・テキスト表示・カラーパレット等は型の語彙として定義済み (SPEC §5.2) |
| 個々の機能を指定して動かせる(上物なし・背景だけ等) | `LayerDirective` (Auto / Rest / Pin) で層ごとにエフェクトを固定 (§B.2) |
| spiral 以外の `.scr` にも使える | ビュワーは spiral を知らない。仕様は saver 非依存で、非対応 `.scr` は標準プレビューのみに劣化 |

## A. 採用方式と検討した代替案

### A.1 採用: プロセス分離 + `/p` 埋め込み + JSON Lines 制御チャネル

```
ScrViewer.exe                                   spiral.scr (別プロセス)
 ┌────────────────────────┐  CreateProcess    ┌───────────────────────────┐
 │ ホスト HWND (プレビュー) │ ── /p <hwnd> ──▶  │ 子 HWND + OpenGL 描画      │
 │ コントローラパネル       │    /scrapi:<pipe> │ ScrApiHost (パイプ client) │
 │ ScrApi client           │ ◀══ JSON Lines ══▶ │ 描画スレッドで値を適用     │
 └────────────────────────┘   名前付きパイプ    └───────────────────────────┘
```

### A.2 なぜこの方式か (代替案との比較)

| 案 | 内容 | 判断 |
|---|---|---|
| **A. `/p` 埋め込み + パイプ (採用)** | 標準プレビュー機構を流用。制御は別チャネル | 標準契約に乗るので非対応 `.scr` も表示できる。クラッシュ分離。セーバーの CRT/GL 初期化がそのまま動く。ビュワーは GL を持たなくてよい |
| B. `.scr` を DLL として `LoadLibrary` | エクスポート関数を直接呼ぶ | `.scr` は EXE。`LoadLibrary` しても CRT 起動コード(静的初期化)が走らず、C++ 実装では不安定。ビュワーと同一プロセスになりクラッシュ・GL コンテキスト衝突の危険。**不採用** |
| C. `WM_COPYDATA` 等のウィンドウメッセージで制御 | 追加チャネル不要 | 双方向の非同期イベント(readout の更新)・大きなマニフェストの転送に向かない。デバッグ性も低い。**不採用** |
| D. 共有テクスチャでビュワー内に描画 | D3D/GL 共有サーフェス | 効果は大きい(合成・録画可)が、`.scr` ごとに描画経路の改修が必要で汎用性を損なう。将来拡張に回す (SPEC §9) |

**採用理由の補足**: マニフェストは実行時に `manifest` 操作で取得する。リソース埋め込み(ビルド時生成)にすれば
`.scr` を実行せずに一覧できるが、初期段階では二重管理になるため見送り、`.scr` の実行前判定が必要になった
時点で追加する (§D.3 フェーズ 5)。

> **リスク**: Windows の慣習上 `.scr` は未知の引数を無視するが、全てがそうとは限らない(設定ダイアログを
> 出す・即終了する等)。ビュワーは接続タイムアウトとプロセス終了検知で必ずフォールバックする (SPEC §3)。
> ホスト HWND を持たない起動(`/scrapi` のみ)は仕様外。

## B. spiral-suction-saver の拡張

### B.1 全体構成

```
src/scrapi/                 (新規・プラットフォーム非依存。Linux でも単体テスト可能)
  Json.{h,cpp}                最小 JSON パーサ/シリアライザ (外部依存ゼロ方針を維持)
  Manifest.{h,cpp}            マニフェスト構造体・検証・JSON 化/パース
  Protocol.{h,cpp}            メッセージ封筒のエンコード/デコード・エラーコード
  ControlModel.{h,cpp}        値ストア・型検証・クランプ・visibleWhen 評価 (ビュワー/セーバー共用)
  ServerCore.{h,cpp}          セーバー側: 要求→ControlRegistry の呼び出し→応答 (転送非依存)
  ClientCore.{h,cpp}          ビュワー側: 要求の採番・応答対応付け・値キャッシュ・イベント処理
  ITransport.h                行単位の送受信の抽象 (パイプ実装は platform 側)

src/core/
  SpiralControls.{h,cpp}      (新規) spiral 固有のマニフェスト生成と ControlRegistry への束縛
                              -- EffectCatalog/EngineConfig だけに依存するので Linux でテスト可能
  effects/EffectParams.h      (変更) LayerDirective を追加 (B.2)
  effects/EffectScheduler.cpp (変更) 固定(Pin)の分岐を追加
  effects/EffectEngine.*      (変更) 実行時の設定変更・状態問い合わせ・時間制御 (B.3)

src/platform/win32/
  ScrApiPipe.{h,cpp}          (新規) 名前付きパイプの ITransport 実装 (client 側)
  ScrApiHost.{h,cpp}          (新規) 受信スレッド + 描画スレッドへの受け渡しキュー
  WinMain.cpp                 (変更) `/scrapi:<pipe>` の解析
  SaverWindow.cpp             (変更) プレビューの論理解像度化・リサイズ追従・ScrApiHost 組み込み
  AppController.*             (変更) コントロール適用・状態公開・コンテンツ源の差し替え

src/viewer/win32/             (新規) ScrViewer 本体 (§C)
```

**方針**: 「コントロールの宣言・値の検証・適用ロジック」はすべて `src/scrapi/` と `src/core/` に置き、Win32 側は
転送とウィンドウ配線だけにする。既存リポジトリの「純粋ロジックは Linux でテスト、Win32 は CI の MSVC ビルド」
の分業をそのまま踏襲する。

### B.2 「個別に指定して動かす」ための `LayerDirective`

現状の `EffectEngine` は、層ごとに候補プールからランダム選択し続ける(`EffectScheduler::Pick`)。
既に `scriptedForeground/Background` があるが、「1 つを固定」「何も動かさない」「ランダムに戻す」の切り替えを
実行時に行う手段としては不十分(履歴による重複排除・種別ごとの選択・遷移の扱いが絡む)。

```cpp
// core/effects/EffectParams.h (追加)
enum class LayerMode { Auto, Rest, Pin };
struct LayerDirective {
    LayerMode mode = LayerMode::Auto;   // Auto: 従来どおり / Rest: エフェクトを掛けず静止表示 (層自体は残る)
    EffectId pinned = EffectId::FlagWave; // mode==Pin のとき有効。Continuous/Terminal どちらでも可
    bool loopTerminal = true;           // Pin が Terminal のとき、終了後に再実行するか(false なら Rest に戻る)
};
// EngineConfig に: LayerDirective foregroundDirective, backgroundDirective;
```

- **Auto**: 既存動作のまま。
- **Rest**: 層の状態機械を既存の `ForceRest()` へ。背景は壁紙そのもの、上物は静止した実キャプチャ断片。
- **Pin**: `EffectScheduler::Pick` の冒頭で、その層が Pin ならその `EffectId` を返す(履歴排除・カタログ種別判定を
  バイパス。継続型は再選択のたびに同じエフェクトを新しいシードで再開、終端型は `loopTerminal` に従う)。
  切り替え時は `EffectStateMachine::Interrupt()`(新設: 現在のエフェクトを `Exiting` に落とす)を呼び、
  既存の包絡線でなめらかに切り替わる。
- 「上物用は何も指定せず背景用だけ動かす」は `foreground = Rest` + `background = Pin` で表現できる。
  上物を**消したい**(空にしたい)場合は §B.4 のコンテンツ源 `none` を使う。
- 背景終端エフェクト `BackgroundSuction` は v2.1.0 時点で通常運用では呼ばれない(§9.10)が、Pin なら
  プレビューできる。「死んでいたコードを検証できる」副次的な利点。

Pin/Rest への切り替えと同時に、フェーズ機械の自動巡回(`STATE_CONTENT → FADEOUT → BLACK → …`)は
**ビュワー実行中は既定で停止**する(`phase.autoCycle=false`、B.3)。固定したエフェクトが 2 分後に黒転して
検証を邪魔しないようにするため。

### B.3 `EffectEngine` / `AppController` の変更

| 変更 | 内容 |
|---|---|
| 設定の実行時変更 | `EngineConfig` は `EffectEngine` が値保持し、状態機械はそのポインタを参照している。`MutableConfig()` を公開し、`ApplyConfigChange()` で次の選択から反映。共通パラメータ(`enabled/intensity/weight/min/maxSeconds`)は既に**毎フレーム参照**(`EffectEngine.cpp` で `ParamsFor(...).intensity`)なので即時反映される |
| 状態の公開 | `EffectEngine::Status()` → 層ごとの `{現在の EffectId, FxState, 経過秒, 残り秒}`。readout (`fg.currentEffect` 等) の元 |
| 時間制御 | `Update(dt)` の前に `dt * timeScale`、`paused` で 0、`step` で 1 フレーム分だけ通す |
| シード | `scrapi.seed`: `rng_` を再構築して `Restart`。エフェクトは既に `ctx.seed` から自前 RNG を作る(D-11)ので、同一シードで再現できる |
| 巡回の停止 | `AppController::SetAutoCycle(bool)`: false の間は `stateMachine_` を `STATE_CONTENT` に保持し、`blackoutTimer_` を進めない |
| 明示的な遷移 | `phase.force` (enum) / `blackout.now` (button): `OnStateEntered` を直接呼んで各フェーズを検証できる |
| コンテンツ源の差し替え | `SetForegroundContent(DecodedImage capture, std::vector<PixelRect> windowRects)` を追加し、`Initialize` 内のマスク生成〜上物レイヤー構築部分を関数として切り出して再実行可能にする (§B.4) |

エフェクト固有パラメータ(`FlagWaveParams` 等。現状は各エフェクトが既定値を内部に持ち、`config.ini` の対象は
共通 5 項目のみ)を実行時に変えるには、`EffectContext` 経由でエフェクトに渡す経路が要る。
**フェーズ 4 の範囲**とし、共通パラメータ(即時反映)だけで最初の価値を出す(§D.3)。

### B.4 プレビュー描画の変更 (`/p` + `/scrapi` のとき)

1. **論理解像度 + ビューポート拡縮**: 現在プレビューは起動時の親クライアント矩形をそのまま「画面サイズ」として
   グリッド・メッシュを構築し、以後サイズ変更に追従しない。SCRAPI 時は固定の論理解像度(既定: プライマリ
   モニタと同じ。制御で変更可)で `AppController` を初期化し、`SetupOrthoProjection2D` の `glOrtho` は論理サイズ、
   `glViewport` は実ウィンドウサイズに分離する(現状は同一の引数)。`WM_SIZE` ではビューポートだけを更新する。
   → 実装上ビュワーが子 HWND を任意にリサイズでき、エフェクトの数式(px 単位)が解像度に依存しない。
2. **コンテンツ源**: 現状 `/p` は画面キャプチャを行わないため上物が空になる (`PreviewMode`)。上物を検証する
   ため、制御可能な入力源を用意する。

   | `content.source` | 内容 |
   |---|---|
   | `sample`(既定) | 内蔵の合成デスクトップ(アイコン列・複数の窓・タスクバーを描いた決定的な画像と窓矩形)。Windows 依存なし・再現可能 |
   | `images` | `content.capture` / `content.wallpaper` に指定した画像 2 枚から、本番と同じ `ComputeContentMask` → 矩形補完 → `RefineBoundaryMask` で上物を作る。過去の `debug_capture.bmp` 等でマスク検出の**検証**ができる |
   | `desktop` | 実機のデスクトップをその場でキャプチャ(ビュワー自身の窓も写る点に注意。`content.refresh` で再取得) |
   | `none` | 上物レイヤーを空にする |

3. **マスクの可視化**: `mask.overlay` (bool) で、検出セル・採用した窓矩形・境界細分化セルを色分け表示する
   デバッグ描画を追加。マスク検出アルゴリズムの調整をビュワー上で対話的に確認できる。

### B.5 Win32 側の配線 (`ScrApiHost`)

- `WinMain`: `/scrapi:<pipe>` を解析。`/p` のときだけ `RunPreview` に渡す(`/s` `/c` では無視)。
- 接続: 別スレッドで `CreateFileW(\\.\pipe\<name>)`(5 秒以内にリトライ)→ 行単位で読み、`ServerCore` に渡す。
- 適用: 受信した `set/invoke` は `ScrApiHost` のロック付きキューに積み、**描画ループのフレーム先頭**
  (`RunMessageLoop` の `Update` 前)で取り出して適用し、応答を書き戻す。`readout` は描画スレッドが
  `changed` イベントを最大 `maxHz` で送る。
- 失敗時: パイプ切断・不正入力ではログに残して SCRAPI だけを停止し、プレビューは継続(SPEC §7)。

### B.6 spiral のコントロール一覧 (マニフェスト案)

`EffectCatalog` から**自動生成**する(効果の追加・削除でマニフェストが自動追随。手書き二重管理をしない)。

```
[group] layers
  [group] background
    bg.mode           enum   auto | rest | pin                      (radio)
    bg.effect         enum   BackgroundContinuousCatalog + BackgroundSuction   (dropdown)
                             visibleWhen bg.mode == pin
    bg.loopTerminal   bool   visibleWhen bg.mode == pin
    bg.minSeconds / bg.maxSeconds   float slider (層の既定持続時間)
  [group] foreground
    fg.mode / fg.effect / fg.loopTerminal / fg.minSeconds / fg.maxSeconds   (同上、上物側カタログ)
[group] effectParams                   -- 各 EffectId ごとに 1 グループ (visibleWhen: その効果が選択中)
    fx.<Id>.enabled  bool
    fx.<Id>.intensity float slider 0..1 (knob 表示可)
    fx.<Id>.weight    float
    fx.<Id>.minSeconds / maxSeconds
    (フェーズ4) fx.<Id>.<固有パラメータ>  例: fx.FlagWave.hz, fx.FlagWave.ampRatio
[group] content
    content.source    enum   sample | images | desktop | none
    content.capture   path   (visibleWhen source == images)
    content.wallpaper path   (visibleWhen source == images)
    content.refresh   button (visibleWhen source == desktop)
    mask.overlay      bool
    mask.refineDepth  int slider 0..4   (boundaryRefineMaxDepth)
    mask.rectEvidence float slider      (rectMinEvidenceFraction)
[group] phase
    phase.autoCycle   bool   (既定 false)
    phase.force       enum   content | fadeout | black | fade | reset
    blackout.now      button
[group] simulation
    scrapi.paused / scrapi.timeScale / scrapi.step / scrapi.seed / scrapi.restart
    particles.preset  enum   Low|Mid|High|Max  (apply: restart)
    bg.image          path   (壁紙の上書き。apply: restart)
[group] status  (readout)
    fg.currentEffect  text      bg.currentEffect  text
    fg.state          text      bg.state          text
    phase.current     text      mask.cellCount    number    scrapi.fps  number
```

「エフェクトを個別に指定」は `bg.effect` / `fg.effect` のプルダウンで、「上物なしで背景だけ」は
`fg.mode = rest`(または `content.source = none`)で実現する。

## C. ビュワー `ScrViewer`

### C.1 技術選定

| 項目 | 選定 | 理由 |
|---|---|---|
| 言語/UI | **C++17 + Win32 + コモンコントロール v6** | 本リポジトリの「外部依存ゼロ」を維持。ウィジェット語彙(スライダ=trackbar、プルダウン=combobox、ラジオ/チェック=button、テキスト表示=static/edit、カラー=`ChooseColor` + スウォッチ)が標準部品でほぼ賄える |
| ノブ | 自前描画コントロール(ドラッグで回転、Shift で微調整) | 標準部品がないため。SPEC は `presentation: knob` を任意扱いにしており、未実装ならスライダに劣化 |
| カラーパレット | スウォッチ列 + `ChooseColor` | `palette` があれば列、なければピッカーのみ |
| 代替 | Dear ImGui (ベンダリング) | 動的 UI とノブが非常に楽だが、D3D/GL バックエンドと第三者コードが増える。**§E-1 で決めたい** |

### C.2 構成とプレビュー埋め込み

```
src/viewer/win32/
  ViewerMain.cpp        WinMain・メッセージループ・メニュー(開く/最近使った/終了)
  HostWindow.{h,cpp}    左: プレビュー領域 (WS_CLIPCHILDREN の子 HWND)  右: コントローラパネル
  ScrLauncher.{h,cpp}   .scr 起動 (CreateProcess) / パイプ作成 / 接続待ち / プロセス終了監視
  SessionController.*   ClientCore を保持: hello→manifest→subscribe の手順、再接続、エラー表示
  ControlFactory.*      manifest ノード → ウィジェットの生成と値の双方向バインド
  Knob.{h,cpp}, Swatch.{h,cpp}   自前ウィジェット
  Layout.*              縦積み + グループ(タブ/折りたたみ)のレイアウト・スクロール
```

**起動手順**: ①ユーザーが `.scr` を開く(ダイアログまたはドラッグ&ドロップ) → ②パイプを作成して待ち受け
→ ③`CreateProcess("x.scr /p <hwnd> /scrapi:<pipe>")` → ④接続待ち(3 秒)→ ⑤`hello` → ⑥`manifest` → ⑦UI 生成 →
⑧`subscribe`(readout)。④が失敗しても、プレビューは標準表示のまま「SCRAPI 非対応(または接続失敗)」と表示。

**プレビューのリサイズ**: `hello` 応答の `viewportHwnd` を `SetWindowPos` で追従させる(`viewport.resize`
capability があるとき)。セーバーは論理解像度を保ち、ビューポートだけを更新する (§B.4-1)。

### C.3 マニフェスト → UI のマッピング

`ControlFactory` は型ごとに 1 つの生成関数を持つだけで、セーバーごとのコードは持たない。

| type | ウィジェット | 操作 → 送信 |
|---|---|---|
| `enum` | dropdown=combobox / radio=ラジオ群 / list=listbox | 選択変更 → `set` |
| `flags` | チェックボックス群 | 変更 → `set`(配列全体) |
| `bool` | チェックボックス | トグル → `set` |
| `int`/`float` | slider=trackbar+数値表示 / knob=自前 / spinner=updown | ドラッグ中は 30ms 程度にスロットルして `set`、離した時に最終値を確定送信 |
| `string` | edit (`multiline` なら複数行) | Enter/フォーカス喪失で `set` |
| `color` | スウォッチ + ピッカー | 確定で `set` |
| `path` | edit + 参照ボタン | 確定で `set` |
| `button` | ボタン | `invoke`(`confirm` があれば確認ダイアログ) |
| `readout` | static / progress(gauge, bar) | `changed` イベントで更新(操作不可) |
| `group` | セクション見出し / タブ / 折りたたみ | - |
| 未知の型 | 読み取り専用の汎用テキスト | - |

- `visibleWhen` / `enabledWhen` は `ControlModel` が値キャッシュで評価し、値が変わるたびに再評価するだけ
  (ラウンドトリップなし)。
- `set` の応答に含まれる**適用後の値**で UI を更新する(クランプ結果の反映、他コントロールとの連動)。
- 「既定に戻す」(各行の小ボタン)は `default` を `set` する。
- ビュワー側の保存: 最後に開いた `.scr` のパスとウィンドウ配置のみ(`%APPDATA%\ScrViewer\viewer.ini`)。
  コントロール値のプリセット保存(名前付き JSON)はフェーズ 5。

### C.4 画面イメージ

```
┌─ ScrViewer ── spiral.scr ─────────────────────────────────────────────────┐
│ ファイル  表示                                                               │
├───────────────────────────────────────────────┬─────────────────────────────┤
│                                               │ ▼ 背景                      │
│                                               │   ◉ auto ○ rest ○ pin        │
│                                               │   エフェクト [ Ripple      ▾]│
│          (プレビュー: spiral.scr の子HWND)      │   持続 ──●────── 5.0–10.0s   │
│                                               │ ▼ 上物                      │
│                                               │   ○ auto ◉ rest ○ pin        │
│                                               │ ▶ コンテンツ / マスク         │
│                                               │ ▶ フェーズ                  │
│                                               │ ▼ 状態                      │
│                                               │   bg: Ripple (Running 3.2s)  │
│  ⏸ ▶| 速度 ──●── 1.0x   seed [12345] ⟲         │   fps 60  マスク 5288 セル    │
└───────────────────────────────────────────────┴─────────────────────────────┘
```

## D. 検証計画と段階計画

### D.1 技術検証(スパイク。設計を確定する前に潰すべき不確実性)

| ID | 検証項目 | 合格条件 |
|---|---|---|
| S1 | 別プロセスの `.scr` が、ビュワーの子ウィンドウ(異プロセス親)で OpenGL 描画でき、親のリサイズに `SetWindowPos` で追従できるか。DPI 100%/150% で座標・サイズがずれないか | 既存 spiral.scr を無改造で `/p` 起動しウィンドウ内に表示。リサイズ後にビューポートだけ更新する最小改修で崩れない |
| S2 | 名前付きパイプの JSON Lines 往復を、`/p` 実行中のセーバーで実現できるか(受信スレッド→描画スレッド受け渡し) | `hello`→`manifest`→`set` が 1 フレーム以内に反映される |
| S3 | 非対応 `.scr`(Windows 標準の `Bubbles.scr` 等)に `/scrapi:` を付けて起動したときの挙動 | 標準プレビューのまま動く、または終了を検知してフォールバックできる |
| S4 | `apply` 中の `EffectEngine` の設定変更(Pin 切り替え)で状態機械が破綻しないか | Auto↔Pin↔Rest の任意の連続切り替えでクラッシュ・固着がない(単体テストで網羅) |

### D.2 自動テスト

- **Linux 単体テスト**(既存の `tests/` ハーネス): `Json`・`Manifest`(検証と往復)・`Protocol`・`ControlModel`
  (クランプ・型検証・`visibleWhen`)・`ServerCore`/`ClientCore` を**メモリ内ループバック転送**で結合して
  hello→manifest→set→changed の一連を検証。`SpiralControls` はマニフェストの**ゴールデンテスト**
  (全 `EffectId` が漏れなく含まれる・id 重複なし・既定値が `MakeDefaultEngineConfig` と一致)。
  `LayerDirective` は `EffectScheduler`/`EffectStateMachine` のテスト(Pin が履歴排除を無視する、
  Terminal の Pin がループ/Rest 復帰する、切り替えで `Interrupt` が包絡線を経由する)。
- **Windows**: CI の `windows-build` (MSVC) で `.scr` と `ScrViewer.exe` の両方をビルド(コンパイル確認)。
  パイプ/GL/ウィンドウの実動作は実機スモーク(下記チェックリスト)。

### D.3 段階計画

| フェーズ | 内容 | 完了の目安 |
|---|---|---|
| 0 | スパイク S1-S4 | 設計の前提(§A, §B.4, §C.2)が実測で確認できた |
| 1 | `src/scrapi/` + テスト、`SpiralControls`(共通パラメータのみのマニフェスト)、`LayerDirective` | Linux テスト全通過。Win32 未着手 |
| 2 | セーバー側 `ScrApiHost`・`/scrapi` 解析・プレビューの論理解像度化 | 手動で JSON を送って層ごとの固定が動く |
| 3 | `ScrViewer` の骨格 (起動・埋め込み・マニフェスト→標準ウィジェット) | 背景/上物のプルダウンでエフェクトを個別にプレビューできる(最初の到達点) |
| 4 | コンテンツ源 (`sample/images/desktop/none`)・マスク可視化・エフェクト固有パラメータ・readout | マスク検出の検証をビュワー上で対話的にできる |
| 5 | ノブ・パレット・プリセット保存・時間制御の仕上げ、マニフェストのリソース埋め込み(実行前判定)、ドキュメント整備 | 汎用ビュワーとして第三者の SCRAPI 対応 `.scr` に使える |

### D.4 実機スモークチェックリスト(フェーズ 3 到達時)

1. `spiral.scr` を開くとプレビューが出て、コントローラが 5 秒以内に現れる。
2. 背景を `pin: Ripple`、上物を `rest` にすると、背景だけが動き上物は静止している。
3. 上物を `pin: VortexSuction`(終端)+ ループにすると、吸い込み→再出現を繰り返す。
4. プレビュー領域をリサイズしても、絵の比率・エフェクトの見た目が崩れない。
5. `Bubbles.scr` など非対応の `.scr` を開いても、標準プレビューのみで落ちない。
6. ビュワーを閉じると `.scr` プロセスが残らない(親消滅の検知で終了)。
7. 本番(`/s`)の起動・終了・設定ダイアログ(`/c`)が従来どおり動く(リグレッションなし)。

## E. 未決事項(設計判断が必要なもの)

| # | 論点 | 推奨 | 代替 |
|---|---|---|---|
| E-1 | ビュワーの UI 基盤 | ネイティブ Win32 (依存ゼロ、ノブは自作) | Dear ImGui (楽だが依存増) |
| E-2 | 仕様の置き場所・名称 | 当面このリポジトリの `docs/SCRAPI_SPEC.md`。第三者採用を見込むなら将来分離 | 最初から別リポジトリ |
| E-3 | ビュワーのリポジトリ | このリポジトリ内 (`src/viewer/`、共通コード `src/scrapi/` を共有) | 別リポジトリ + サブモジュール |
| E-4 | `.scr` 側のマニフェスト伝達 | 当面は実行時 `manifest` のみ。リソース埋め込みはフェーズ 5 | 最初からビルド時生成リソース |
| E-5 | エフェクト固有パラメータの公開範囲 | フェーズ 4 で `EffectContext` 経路を追加して段階公開 | 共通 5 項目のみで止める |
| E-6 | 名前 (`SCRAPI` / `ScrViewer`) | 仮称のまま進める | 変更 |

## F. 実装状況と設計からの変更点 (随時更新)

| フェーズ | 状況 |
|---|---|
| 0 スパイク | **S4 完了**(`tests/test_fx_Directive.cpp` の無作為切り替えテスト)。S2 の受け渡し設計は実装済み(実機の動作は未確認)。**S1 (別プロセス子ウィンドウでの GL 描画とリサイズ追従)・S3 (非対応 `.scr` の挙動) は実機確認待ち** |
| 1 コア | **完了**。`src/scrapi/`(Json/Manifest/ControlModel/Protocol/ServerCore/ClientCore)、`SpiralControls`(マニフェスト生成・バインダ)、`LayerDirective`、`SimulationClock`。Linux 単体テスト 315 件通過 |
| 2 セーバー側 | **実装済み・実機確認済み**(接続、エフェクト固定、リサイズ追従)。`/scrapi:<pipe>`、`ScrApiPipe`/`ScrApiHost`、プレビューの論理解像度化、自動巡回の停止 |
| 3 ビュワー骨格 | **実装済み・実機確認済み**。パネルは Direct2D の自前描画(`docs/DESIGN_VIEWER_UI.md`)。コマンドバー・操作バー・状態バーの新デザイン化は未着手 |
| 4 コンテンツ源 ほか | **一部実装**。上物の入力源 `content.source`(`sample`=内蔵の合成デスクトップ / `none`)、`mask.overlay`(検出セルと窓矩形の重ね描き)、`scrapi.seed`/`scrapi.restart`、`content.info` を実装(実機確認待ち)。合成デスクトップは本番と同じマスク処理(`core::FinishContentMask`)を通す。**未実装**: `images`/`desktop` の入力源、エフェクト固有パラメータ(FadeOutIn の下限など)、`phase.*` |
| 5 仕上げ | **未着手**(ノブ、パレットのスウォッチ列、プリセット、マニフェストのリソース埋め込み、DPI 対応) |

### 設計からの変更点

- **`invoke` の対象は `control`**: 封筒の `id`(要求番号)と衝突するため(SPEC §4.2)。単体テストで発見。
- **`set` の応答は id ごとの結果**(`results`)。一部が不正でも有効な分は適用される(SPEC §4.2)。
- **制御 `bg.pool` / `fg.pool`(flags)を追加**: Auto モードが選べるエフェクトの集合。§B.6 の `fx.<Id>.enabled` の代わり。
  `fx.<Id>.*` は Pin 時に意味のある `intensity`/`minSeconds`/`maxSeconds` のみ。
- **`scrapi.restart` / `scrapi.seed` / `content.*` / `mask.*` / `phase.*` は未実装**(Phase 4)。
- **ビュワーのファイル構成**: 設計の `HostWindow`/`SessionController`/`ControlFactory` は、`ViewerMain.cpp`(ウィンドウと
  セッション)と `ControlPanel.cpp`(ウィジェット生成と同期)、`ScrLauncher.cpp`(プロセス起動)に集約した。
- **パイプは共有実装**: `ScrApiPipe`(クライアント/サーバー両モード)を `.scr` とビュワーで共有。
- **バージョン文字列**: `core::kAppVersion`(`src/core/Version.h`)。リリースはタグ駆動でコードを変えないため、
  リリース時に更新するかは運用次第(現状は `2.1.0+scrapi-dev`)。
- **既知の未対応**: ビュワーは DPI 非対応(拡大表示時はビットマップ拡大)。スライダの `log` スケール、`knob` 表示、
  `folder` 型パス選択は標準スライダ/ファイル選択に劣化する。

