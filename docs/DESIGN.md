# Spiral Suction Saver 設計書

対象: `要件.txt` に基づく Windows スクリーンセーバー (.scr)。
本書は `skil.md` の定める「設計書は第三者が読んでも実装可能なレベルまで詳細化する」を満たすことを目的とする。

## 1. 全体アーキテクチャ

コアロジック (`src/core/`) を Win32/OpenGL 実装 (`src/platform/win32/`) から完全に分離した。
`src/core/` は Windows ヘッダに一切依存しない C++17 のみで書かれており、Linux 上でも
ビルド・単体テストできる。これは開発機が Linux (WSL2) で Windows/MSVC/MinGW-w64 が
未導入という制約への対応であり、かつ「小さな関数・小さなモジュール」への分割にも資する。

```mermaid
flowchart TB
    subgraph core["src/core/ (プラットフォーム非依存, Linux でも単体テスト可能)"]
        SpiralMath
        SuctionCenterWalker
        ContentMask
        WallpaperFit
        ParticleGrid
        StateMachine
        FadeController
        ConfigModel
        GpuTierClassifier
        RandomSource
        Logger
    end

    subgraph win32["src/platform/win32/ (Windows専用, CIのwindows-latestで実ビルド)"]
        WinMain --> SaverWindow
        SaverWindow --> OpenGLContext
        SaverWindow --> AppController
        AppController --> Renderer
        AppController --> ImageLoader
        AppController --> WallpaperProvider
        ConfigDialogWin32 --> OpenGLContext
        WinMain --> ConfigDialogWin32
        FileLogSink --> Logger
        ConfigDialogWin32 --> WinFileIO
        AppController --> WinFileIO
    end

    win32 -->|core:: 型を利用| core
```

## 2. モジュール構成

### 2.1 src/core/

| モジュール | 責務 |
|---|---|
| `SpiralMath` | θ+=dTheta, r-=speed のらせん軌道計算 (要件§4, §7)。`SpiralState`/`SpiralParams`/`StepSpiral`。`SpiralParams::centerAccelFactor`(>0)で中心に近づくほど角速度が増し、らせん状に歪む(追加要望対応、既定0で従来どおり)。`MakeParamsForRevolutions(r0, suctionSpeed, targetRevolutions)`は、開始半径`r0`から逆算したdThetaを返し、r0の大小にかかわらずほぼ指定回転数で中心に消えるようにする(追加要望: らせん回転をもっと緩やかにし3〜5周回するくらいにする)。 |
| `SuctionCenterWalker` | 吸い込み中心のランダムウォーク (1〜3px/frame 相当、画面端で反射)。`IRandomSource` を注入して決定的にテスト可能。 |
| `ContentMask` | 実画面キャプチャと壁紙画像をグリッドセル単位で差分判定し、「差分ブロック(=実際のアイコン/タスクバー/開いているウィンドウなど)」のセルだけを`true`にした`bool`配列を返す。比較対象の壁紙画像は`WallpaperFit`で画面サイズへ合成済みのものを渡す。Windows非依存の純粋関数群。 |
| `WallpaperFit` | Windowsの壁紙表示設定(中央/タイル/ストレッチ/フィット/塗りつぶし/スパン)をそれぞれ再現して壁紙画像を画面サイズへ合成する`CompositeWallpaper`。単純な引き伸ばしは「ストレッチ」にしか一致しないため、`ContentMask`の比較基準を実際の表示と正しく合わせるために必要 (§9.2)。Windows非依存の純粋関数群。 |
| `ParticleGrid` | 背景画像を NxN 粒子に分割 (要件§5)。`ComputeGridDimensionForParticleCount` が希望粒子数から N を逆算。差分ブロックフェーズもこの同じグリッドを`ContentMask`で絞り込んだ部分集合を使う。 |
| `StateMachine` | `STATE_CONTENT→BACKGROUND→BLACK→FADE→RESET→CONTENT` の純粋な遷移関数 (要件§8)。`STATE_CONTENT`は旧`STATE_ICONS`/`STATE_WINDOWS`を統合したもの(§9参照)。 |
| `FadeController` | 黒→背景画像のフェード (alpha 0→1、時間ベース)。 |
| `ConfigModel` | 粒子数プリセット(Low/Mid/High/Max/Auto/Custom)+背景画像上書きパスと、iniテキストとの相互変換。不正な値は既定値にフォールバックする。 |
| `GpuTierClassifier` | GL_VENDOR/RENDERER/VERSION 文字列→粒子数の純関数 (要件§6 自動判定)。実GLコンテキスト不要でテスト可能。 |
| `RandomSource` | `IRandomSource` の std::mt19937 実装 (本番用)。テストは別途フェイク実装を使う。 |
| `Logger` | sink注入型の最小ロガー。本番はファイル出力、テストはメモリキャプチャに差し替え可能。 |

### 2.2 src/platform/win32/

| モジュール | 責務 |
|---|---|
| `WinMain.cpp` | エントリポイント。`/s /c /p` 引数を解析しディスパッチする。 |
| `SaverWindow` | フルスクリーン(/s)またはプレビュー子ウィンドウ(/p)の作成とメインループ (60fps目標、Update/Draw/SwapBuffers)。 |
| `OpenGLContext` | PIXELFORMATDESCRIPTOR設定 + `wglCreateContext` + ダブルバッファ。GPU自動判定のための一時コンテキスト作成にも使う。 |
| `AppController` | 状態機械・らせん状態・粒子・タイマーを保持し、`Update(dt)`/`Draw()` で要件§4の吸い込み順序を実行するオーケストレータ。起動時に`core::ContentMask`で差分ブロックを決め、`core::ParticleGrid`の部分集合として吸い込み対象を持つ。 |
| `Renderer` | 固定機能OpenGLの描画バッチ関数群 (全画面クアッド・粒子をそれぞれ `glBegin`/`glEnd` 1回で描画、要件§7)。差分ブロック・背景粒子はいずれも同じ`DrawParticlesBatched`を使う。 |
| `ImageLoader` | Windows Imaging Component (WIC) でBMP/JPEG/PNG/GIFをデコードしGLテクスチャ化。外部画像ライブラリ不要。 |
| `ScreenCapture` | `BitBlt`で画面を1回読み取り`DecodedImage`化。`AppController`がこれを壁紙画像と差分判定し、差分ブロックのテクスチャとしても使う。読み取り専用。 |
| `WallpaperProvider` | `SystemParametersInfoW(SPI_GETDESKWALLPAPER)` で現在の壁紙パスを取得(読み取り専用)。パスが空(壁紙が画像ではなく単色背景に設定されている場合、Windowsはエラーではなく空文字列を返す)の場合に備え、`GetSysColor(COLOR_DESKTOP)`で実際の単色背景色を取得する`GetSystemDesktopColor`も提供する。また`HKCU\Control Panel\Desktop`の`WallpaperStyle`/`TileWallpaper`(読み取り専用)から実際の壁紙表示設定を判定する`GetSystemWallpaperFitMode`も提供し、`core::WallpaperFit`に渡す。 |
| `ConfigDialogWin32` | `/c` 設定ダイアログ (プリセットコンボ、カスタム数値、Auto検出結果表示、壁紙上書き選択)。 |
| `AppPaths` / `WinFileIO` | `%APPDATA%/SpiralSuctionSaver/{config.ini,saver.log}` の解決とワイド文字パスでのファイルI/O (非ASCIIユーザー名対策)。 |
| `FileLogSink` | `core::Logger` にファイル出力シンクを登録 (1MB超でローテート)。 |
| `StringConvert` | UTF-8 (core側の文字列表現) ⇄ UTF-16 (Win32 API) 変換。 |

## 3. 状態遷移

```mermaid
stateDiagram-v2
    [*] --> STATE_CONTENT
    STATE_CONTENT --> STATE_BACKGROUND: 全差分ブロック消滅
    STATE_BACKGROUND --> STATE_BLACK: 全粒子消滅
    STATE_BLACK --> STATE_FADE: 一定時間経過
    STATE_FADE --> STATE_RESET: alpha=1到達
    STATE_RESET --> STATE_CONTENT: 一定時間経過（無限ループ）
```

各フェーズの詳細:

- **STATE_CONTENT**: 背景画像(壁紙)を全画面に描画したうえで、`core::ContentMask`が実画面
  キャプチャと壁紙の差分から検出した「差分ブロック」(実際のアイコン・タスクバー・開いている
  ウィンドウなど、壁紙の上に何か描かれている箇所)だけを、`ScreenCapture`のキャプチャ画像を
  テクスチャにして`SpiralMath`で中心へ吸い込む。差分のなかったセルは最初から描画対象に
  含まれないため、背景の壁紙がそのまま見え続ける(=「透明化」)。各ブロックは
  `MakeParamsForRevolutions`で個別に算出したdThetaにより、開始距離に関わらずほぼ3〜5周回
  してから中心に消える(追加要望対応)。実画面キャプチャが無い場合(プレビュー時・取得失敗時)は
  差分ブロックが0件になり、このフェーズは実質的に即座にスキップされて`STATE_BACKGROUND`に進む。
- **STATE_BACKGROUND**: 画面を黒でクリアしてから粒子をバッチ描画するため、吸い込まれた
  箇所から自然に黒が露出する。STATE_CONTENTと同様、各粒子は`MakeParamsForRevolutions`で
  個別に算出したdThetaによりほぼ3〜5周回してから中心に消える(追加要望対応: 背景画像側の
  回転も緩やかにする)。そのベースdThetaに加えて`centerAccelFactor`(既定40)が上乗せされ、
  中心に近づくほど角速度がさらに増してらせん状に歪む演出も維持している(既存の別の追加要望)。
- **STATE_BLACK**: 黒一色を一定時間 (1秒) 保持。
- **STATE_FADE**: 黒背景の上に背景画像を alpha=0→1 でブレンド。
- **STATE_RESET**: 背景(壁紙)と差分ブロックを全て元の位置で静止表示し、一定時間 (1.5秒)
  保持してから `STATE_CONTENT` に戻る。差分ブロックは起動時に検出した同じ位置・同じ内容を
  再利用するため、要件§4 step6 の「元の位置に再描画」を満たす。

## 4. データフロー (1フレーム)

```mermaid
sequenceDiagram
    participant Loop as SaverWindow メインループ
    participant App as AppController
    participant Center as SuctionCenterWalker
    participant Spiral as SpiralMath (対象ごと)
    participant SM as SaverStateMachine
    participant Draw as Renderer

    Loop->>App: Update(dt)
    App->>Center: Step(rng)
    Center-->>App: 現在の中心座標
    App->>Spiral: StepSpiral(state, params, center)
    Spiral-->>App: 新しい位置 / alive
    App->>SM: Advance(inputs)
    SM-->>App: 状態遷移の有無
    Loop->>App: Draw()
    App->>Draw: DrawXxxBatched(...)
    Loop->>Loop: SwapBuffers()
```

## 5. エラーハンドリング方針

| 状況 | 対応 |
|---|---|
| 背景画像の読み込み失敗 (壁紙/上書き画像とも)、または壁紙が単色背景設定でパスが空 | `GetSystemDesktopColor`で実際のデスクトップ単色背景色を取得し、それを単色画像として継続する(取得自体に失敗した場合のみ固定色 (30,40,60) にフォールバック)。クラッシュさせない。 |
| `config.ini` の内容が不正 | 該当フィールドのみ既定値にフォールバック (Mid/3000粒子/上書きなし)。ファイル全体を無視することはしない。 |
| OpenGL コンテキスト生成失敗 | 致命的とみなし、`MessageBox` 表示後にそのモードを終了する (処理を継続できないため)。 |
| `%APPDATA%` が解決できない | ログ出力・設定保存を諦めて既定値で動作を継続する。 |

全 Win32 ハンドル (HWND/HDC/HGLRC/テクスチャ/表示リスト) は RAII (デストラクタでの解放) または
明示的な `Shutdown()` で確実に解放する。スクリーンセーバーは無期限に稼働し得るため、
フレームごとに確保・解放を行わない設計 (バッチ描画・使い回しバッファ) にしている。

## 6. ロギング方針

`core::Logger` はシンク注入型。本番は `platform::InstallFileLogSink()` が
`%APPDATA%/SpiralSuctionSaver/saver.log` への追記シンクを登録し、1MBを超えたら
ローテート（切り詰めて再作成）する。ユニットテストではシンクを設定しない
(またはメモリキャプチャに差し替える) ため、ファイルI/Oなしでロジックを検証できる。

## 7. 設定ファイル仕様

`%APPDATA%/SpiralSuctionSaver/config.ini`:

```ini
[SpiralSuctionSaver]
Preset=Mid
CustomParticleCount=3000
BackgroundImageOverride=
```

- `Preset`: `Low|Mid|High|Max|Auto|Custom` (大文字小文字を区別しない)。
- `CustomParticleCount`: `Preset=Custom` のときのみ使用。正の整数以外は既定値(3000)。
- `BackgroundImageOverride`: 空なら現在の壁紙を自動使用。

## 8. テスト方針

### 8.1 単体テスト (このリポジトリで実行・全緑を完成条件とする)

`tests/` に `src/core/` の各モジュールに対する自作最小テストハーネス
(`tests/test_framework.h`、外部依存ゼロ) ベースのテストを配置。

```bash
cmake -S . -B build-tests -DBUILD_CORE_TESTS_ONLY=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

対象: `SpiralMath` の収束性・`centerAccelFactor`による角速度増加とクランプ挙動・
`MakeParamsForRevolutions`の目標回転数への収束と極小半径時のフォールバック、
`SuctionCenterWalker` の境界反射、`StateMachine` の全遷移経路、
`ParticleGrid` の件数・範囲、`ContentMask` の差分判定(同一/差分セルのみ検出/閾値未満は
無視)とリサンプルの正しさ、`WallpaperFit` の各表示スタイル(中央/タイル/ストレッチ/
フィット/塗りつぶし)の合成結果とレターボックス色、`ConfigModel` のini往復変換と不正値
フォールバック、
`GpuTierClassifier` の既知ベンダ文字列分類、`RandomSource` の値域。

### 8.2 結合テスト

Win32/OpenGL実装はLinux開発機でコンパイルできないため、GitHub Actions
`windows-latest` (MSVC) での実ビルド成功を結合スモークテストとする
(`.github/workflows/build.yml`)。加えて、開発中に **MinGW-w64 をローカルに展開して
クロスコンパイルし、実際に `.scr` (PE32+ GUI 実行ファイル) が生成されることを確認済み**
であり、その過程で以下の問題を発見・修正した:

- `windres` は既定でターゲット依存マクロ (`_WIN32`/`_WIN64`) を定義しないため
  `<windows.h>` のプリプロセスに失敗する → `--preprocessor` に g++ 自身を指定して回避。
- `UNICODE`/`_UNICODE` を定義しないと `LoadCursor` 等の汎用マクロが ANSI 版に展開され、
  `LPCWSTR` を要求する呼び出しと型不一致になる → ターゲットに明示的に定義。
- エントリポイントを `wWinMain` にすると MSVC/MinGW いずれも既定のリンカ設定では
  `WinMainCRTStartup` が `WinMain` を要求し未解決シンボルになる → コマンドラインは
  `GetCommandLineW`/`CommandLineToArgvW` で自前解析しているため、`lpCmdLine` を
  使わない `WinMain` (ANSI シグネチャ) をエントリポイントに採用し、
  追加のリンカフラグなしで両ツールチェーンに対応した。

### 8.3 手動確認チェックリスト (Windows実機)

- [ ] `SpiralSuctionSaver.scr /s` でフルスクリーン起動し、差分ブロック→背景粒子→
      黒→フェードイン→リセットの順にループすることを目視確認する。
- [ ] キー入力・クリック・一定量のマウス移動で `/s` が終了することを確認する。
- [ ] `SpiralSuctionSaver.scr /c` で設定ダイアログが開き、プリセット変更・カスタム値・
      Auto判定結果表示・背景画像の変更ができ、`config.ini` に保存されることを確認する。
- [ ] Windowsの「スクリーンセーバーの設定」プレビュー枠で `/p` によるプレビューが表示され、
      設定ダイアログを閉じるとプレビューも終了することを確認する(プレビューは実画面キャプチャ
      を行わないため差分ブロックフェーズはなく、背景粒子フェーズから始まって見える -- §9参照)。
- [ ] 壁紙を変更した状態で上書き未設定のときに新しい壁紙が反映されることを確認する。
- [ ] `/s` 実行時、実際のアイコン・タスクバー・開いているウィンドウなど壁紙の上に何か
      表示されている箇所だけがブロックとして吸い込まれ、何もない箇所は最初から壁紙が
      見えていることを確認する。デスクトップ背景が単色設定のときも、ブロックが消えた
      箇所にプレースホルダー色ではなく実際の背景色が見えることを確認する(§9.3)。
- [ ] 差分ブロック・背景粒子とも、中心に吸い込まれるまでに目視でおおよそ3〜5回転して
      いることを確認する(粒子ごとに開始位置からの回転数は個体差があってよい)。
      背景粒子側は中心に近づくにつれ、それに加えてさらに回転が速くなりらせん状に
      歪む演出になっていることを確認する。
- [ ] Windowsの壁紙表示設定(塗りつぶし/フィット/中央/タイル等)によって、差分ブロックの
      検出精度が変わらないか確認する(ズレが大きい場合は`core::ContentMaskConfig`の
      `pixelDiffThreshold`/`cellDifferingFraction`を調整する)。
- [ ] 差分ブロック内のアイコンやタスクバーの文字が、縦横比を保ったまま(不自然に
      縦や横へ引き伸ばされることなく)表示されていることを確認する(§9.5)。

## 9. 既知の制約・スコープ外事項

- マルチモニタは対象外とし、プライマリディスプレイの解像度のみを使用する
  (要件.txt に明記がないため、設計レビューでスコープを明示的に限定)。
- 実デスクトップのアイコン・ウィンドウを移動・削除・設定変更する操作は一切行わない
  (要件§10 禁止事項準拠)。以下で述べる画面キャプチャ・差分判定はいずれも
  **読み取り専用**であり、「操作」ではない。

### 9.1 「実際にあった内容から吸い込んでほしい」への対応の変遷

当初の要件§3は「矩形+ラベルのみの抽象表現、位置はランダム生成」だったが、実運用
フィードバックを受けて複数段階で変更した。

1. 「ただの箱に見える」→ 起動直後の画面を1回`BitBlt`で読み取り(`ScreenCapture`)、矩形の
   見た目をそのクリッピングでテクスチャリングするよう変更(位置は依然ランダム)。
2. 「ランダムな位置ではなく実際にあった位置から吸い込んでほしい」→ `EnumWindows`でウィンドウ
   矩形を、アイコン用`ListView`のクロスプロセス読み取りでアイコン矩形を取得し、`DesktopElements`
   の乱数レイアウトの代わりに使うよう変更。重なったウィンドウ/アイコンの見た目のずれ対策として、
   さらに`PrintWindow`で個別ウィンドウ・アイコン層ごとにキャプチャする仕組みも追加した。
3. **(今回)実機で試したところ2の個別クエリ方式は期待した見た目にならなかった**ため、
   個別のアイコン/ウィンドウ矩形という抽象化そのものをやめ、**画面全体のキャプチャと
   壁紙画像の差分だけで「差分ブロック」を決める**方式に置き換えた
   (`EnumWindows`/アイコン`ListView`読み取り/`PrintWindow`個別キャプチャ、および
   `DesktopElements`/`TextRenderer`は削除)。`core::ContentMask`が実画面キャプチャと
   壁紙をグリッドセル単位で比較し、差分のあるセルだけを`AppController`が
   `core::ParticleGrid`の部分集合として吸い込む。差分のないセルは最初から壁紙が
   表示されているため、「差分がない部分は透明化して背景が見えるようにする」という
   要望を自動的に満たす。個々のブロックがどのウィンドウ/アイコンに対応するかという
   意味的な情報は失われるため、**文字ラベルの表示は廃止**した(差分方式では対応関係が
   ないため付けられない)。

  実画面キャプチャ・差分判定のいずれも**位置や見た目を読み取るだけ**で、移動・削除・
  設定変更を行う操作ではないため、要件§10「実際のデスクトップを操作してはならない」には
  抵触しない（操作＝変更を指し、読み取り専用の取得は含まないと解釈）。取得に失敗した場合
  (画面キャプチャ失敗、壁紙デコード失敗等)は、差分ブロックが0件になり`STATE_CONTENT`
  フェーズが実質スキップされるだけで、クラッシュや異常動作にはならない。
  プレビュー表示 (`/p`) は意図的に画面キャプチャを行わない設計を維持しているため、
  差分ブロックのフェーズは表示されず背景粒子フェーズから始まって見える。

### 9.2 壁紙合成方式とのズレの修正 (実機フィードバック)

当初`core::ContentMask`は壁紙画像を`core::ResampleRgba`でニアレストネイバーにより単純に
画面サイズへ引き伸ばして(アスペクト比無視)実画面キャプチャと比較していたが、これは
Windowsの実際の壁紙表示設定のうち「ストレッチ」にしか一致しない。Windows 10/11の既定は
「塗りつぶし」(アスペクト比を保って画面いっぱいになるよう拡大しクロップ)であり、この
方式の違いにより広い範囲で誤って差分ありと判定され、「本来透明なはずの箇所に実キャプチャ
のブロック(≒背景に近い色)が見え隠れする」という実機フィードバックにつながった。

`core::WallpaperFit`(新設)に、Windowsの壁紙表示設定(中央/タイル/ストレッチ/フィット/
塗りつぶし/スパン)をそれぞれ再現する`CompositeWallpaper`を実装し、
`platform::GetSystemWallpaperFitMode`(`HKCU\Control Panel\Desktop`の`WallpaperStyle`/
`TileWallpaper`を読み取り専用で取得)で実際の設定を判定してから比較用の基準画像を
合成するよう修正した。中央/フィットで生じる余白(レターボックス)は
`platform::GetSystemDesktopColor`で取得した実際の単色背景色で塗る(Windowsの挙動と一致)。
スパン(マルチモニタ用)はこのプロジェクトの対象外であるプライマリ画面のみのスコープ
(§9冒頭)に合わせ、塗りつぶしと同じ扱いにしている。

### 9.3 単色背景時の「透明化」不具合の修正 (実機フィードバック)

実機確認で、「差分がない箇所が透明化されているはずが、背景画像未指定時のプレースホルダー
のような無地の領域が見える」という報告があった。原因は、デスクトップの背景が(画像ではなく)
単色に設定されている場合、`GetSystemWallpaperPath`は失敗ではなく**空文字列**を返す
Windowsの仕様で、この場合`AppController`は固定の適当な色 (30,40,60) をプレースホルダーとして
使っていたため、実際の単色背景色と一致していなかった点にある。差分ブロックが消えて
「背景」が露出したときに、この不一致な色が見えてしまっていた。
`platform::GetSystemDesktopColor` (`GetSysColor(COLOR_DESKTOP)`) で実際の単色背景色を
取得してプレースホルダーとして使うよう修正し、壁紙画像デコード失敗時とパス空時の両方の
フォールバックをこの実際の色に統一した。

### 9.4 らせん回転を緩やかにする対応の適用範囲 (実機フィードバック)

当初は差分ブロック(STATE_CONTENT)のみ`MakeParamsForRevolutions`による緩やかな回転
(3〜5周)に変更し、背景粒子(STATE_BACKGROUND)は既存の`centerAccelFactor`(既定40)による
Vortex演出のまま据え置いていたが、「デスクトップ画像が吸い込まれた後の背景画像側も回転が
速すぎる」との実機フィードバックを受け、背景粒子にも同じ`MakeParamsForRevolutions`に
よる緩やかな基準回転を適用するよう変更した。

その後さらに、「まだ速く感じる」との追加フィードバックを受けて調査したところ、基準の
dThetaを緩めても、中心付近で`centerAccelFactor`が上乗せする角速度(`kMaxDTheta`=1.2まで
到達しうる)が非常に大きく、この"仕上げ"部分が全体の速度印象を支配していたことが分かった。
`kParticleCenterAccelFactor`を40から8まで下げ、この加速が効き始める半径を大幅に縮小した
(中心付近のごく短い区間だけの演出になるよう調整)。中心に近づくほど角速度が増す効果自体は
(別の追加要望に基づくものなので)完全には無くさず、弱めて残している。

### 9.5 粒子クアッドが正方形に固定されていたことによる縦伸び (実機フィードバック)

実機確認で、「吸い込まれるブロック内のアイコン画像が縦に伸びている、タスクバーの時刻
文字が不自然に拡大されて見える」との報告があった。原因は`AppController`の
`particleHalfSizePx_`(単一のスカラー値)にあった:
`std::max(cellWidth, cellHeight) * 0.55` としてグリッドセルの**大きい方の辺**を採用し、
それを幅・高さ両方に使う**正方形**のクアッドとして粒子を描画していた。しかし1920x1080の
ような非正方形の画面をN×Nの正方グリッドで分割すると、セル自体は横長(アスペクト比は
画面と同じ)になる。セルの内容(UV座標が示す範囲)は正しく非正方形のままなのに、それを
描画する枠だけ正方形に引き伸ばしていたため、サンプリングされた画像が縦方向に約
(横幅/縦幅)倍(1920x1080・粒子数3000の場合で計算すると約1.8倍)引き伸ばされて見えていた。
背景粒子フェーズ(壁紙のシャッター効果)にも同じ問題が存在していたが、抽象的な画像の
欠片では歪みが目立ちにくく、これまで気づかれていなかった。

`particleHalfSizePx_`を`particleHalfWidthPx_`/`particleHalfHeightPx_`の軸ごとの値に分割し、
`Renderer::DrawParticlesBatched`もこの2値を受け取って矩形(セルと同じアスペクト比)の
クアッドを描画するよう修正した。これによりSTATE_CONTENT・STATE_BACKGROUNDの両フェーズで
歪みが解消される。
