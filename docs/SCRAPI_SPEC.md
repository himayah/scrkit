# SCRAPI v1 仕様 (Screensaver Control API)

> 状態: **実装済み・main に統合済み** (v3.0.0)。ScrKit.scr 側とビュワーの実装あり
> (実装状況は [`DESIGN_VIEWER.md`](DESIGN_VIEWER.md) §F)。実装で見つかった修正(`invoke` の対象は `control` 等)は反映済み。
> 本書は **特定のスクリーンセーバーに依存しない**汎用仕様であり、SCRAPI 対応の `.scr` と
> 汎用ビュワー(`ScrViewer`、実装設計は [`DESIGN_VIEWER.md`](DESIGN_VIEWER.md))の間の契約だけを定義する。
> ScrKit.scr 固有のコントロール一覧は `DESIGN_VIEWER.md` 側にある。

## 1. 目的と設計原則

`.scr` が内部に持つ機能を、スクリーンセーバーとしての動作を変えないまま、外部から個別に
呼び出し・調整・観測できるようにする。ビュワーは `.scr` ごとの専用コードを持たず、
**`.scr` が自己申告するマニフェストからコントローラ UI を自動生成する**。

1. **標準動作を壊さない**: `/s` `/c` `/p` の既存契約は一切変えない。SCRAPI は追加のスイッチ
   `/scrapi` が与えられた `/p` 実行時にだけ有効になる。`/s` と `/c` では無視する。
2. **段階的に劣化する**: SCRAPI 非対応の `.scr` でも、ビュワーは標準の `/p` プレビューだけは
   表示できる(コントローラなし)。
3. **宣言的**: UI の形はマニフェスト(データ)で決まる。ビュワーはウィジェットの語彙だけを知る。
4. **セーバー実装の言語・ツールキットに依存しない**: 転送は JSON Lines、ビュワーとの
   接続はプロセス間のバイトストリーム。C++ 以外の実装も可能。
5. **前方互換**: 未知の型・プロパティ・イベントは無視(または汎用表示)して継続する。

## 2. 用語

| 用語 | 意味 |
|---|---|
| セーバー | SCRAPI 対応の `.scr` |
| ビュワー | セーバーを起動して埋め込み表示し、コントローラを提供する側 |
| セッション | ビュワーとセーバーの 1 回の接続(セーバープロセスの寿命と一致) |
| コントロール | マニフェストが宣言する、値・操作・表示の単位。`id` で一意に識別 |

## 3. 起動と接続

```
saver.scr /p <hwnd> /scrapi:<pipeName>
```

- `<hwnd>`: ビュワーが用意した、プレビュー用の親ウィンドウ(標準の `/p` と同じ)。
- `<pipeName>`: ビュワーが**先に作成して待ち受ける**名前付きパイプ名 (`\\.\pipe\` を除いた部分。
  推奨: `scrapi-<GUID>`)。セーバーはクライアントとして接続する。
  ビュワーは自ユーザー限定 DACL・`PIPE_REJECT_REMOTE_CLIENTS` で作成すること。パイプは**バイトモード**
  (`PIPE_TYPE_BYTE`)、`FILE_FLAG_OVERLAPPED` は任意。行区切りはメッセージ層(§4)が担う。
- セーバーは起動後 5 秒以内に接続を試みる。接続できない、またはスイッチ自体を理解しない
  セーバーの場合、ビュワーは一定時間(既定 3 秒)で接続待ちを打ち切り「標準プレビューのみ」
  として扱う。セーバープロセスが終了した場合も同様(ビュワーは終了を検知してエラー表示)。
- 接続後、セーバーは通常の `/p` プレビューと同様に親ウィンドウの子ウィンドウとして描画を
  続ける。親ウィンドウの消滅を検知して自身を終了する標準の挙動も維持する。
- 転送の抽象化: 本仕様のメッセージ層は「順序保証のあるバイトストリーム」だけを要求する。
  v1 の標準バインディングは名前付きパイプ。将来の別バインディング(匿名パイプ・ソケット等)は
  この節だけを差し替える。

## 4. メッセージ層

**UTF-8 の JSON Lines**: 1 行に 1 個の JSON オブジェクト、行区切りは `\n`。1 メッセージ最大 1 MiB。
JSON 内の改行は `\n` にエスケープする。手動デバッグ(テキストエディタ・`type`)が可能なことを重視した選択。

### 4.1 封筒

```jsonc
// ビュワー → セーバー (要求)
{"id": 12, "op": "set", "values": {"fg.effect": "FlagWave"}}
// セーバー → ビュワー (応答。id は要求と同じ)
{"id": 12, "ok": true, "results": {"fg.effect": {"ok": true, "value": "FlagWave"}}}
{"id": 13, "ok": false, "error": {"code": "unknownId", "message": "no control 'x'"}}
// セーバー → ビュワー (イベント。id なし)
{"ev": "changed", "values": {"fg.currentEffect": "FlagWave"}}
```

- 要求は `id`(整数、ビュワーが採番)と `op` を持つ。応答は同じ `id` で 1 回だけ返る。
- 要求はビュワー→セーバー方向のみ。セーバー→ビュワーは応答とイベントだけ(v1)。
- 応答順は要求順(セーバーは要求を FIFO で処理する)。

### 4.2 操作 (`op`)

| op | 方向 | 内容 |
|---|---|---|
| `hello` | V→S | 最初に必ず送る。`{"apiVersion":"1.0","client":"ScrViewer/0.1"}`。応答: `{"apiVersion":"1.0","saver":{"id","name","version"},"capabilities":[...],"viewportHwnd":<整数>}` |
| `manifest` | V→S | 応答 `{"manifest":{...}}`(§5。`rev` は改訂番号でマニフェスト内に含まれる) |
| `get` | V→S | `{"ids":[...]}` の現在値を `{"values":{...}}` で返す。省略時は値を持つ全コントロール。未知の id が 1 つでもあれば `unknownId` |
| `set` | V→S | `{"values":{id:値,...}}`。**1 メッセージ内は原子的**に(同一フレーム境界で)適用する。応答は `{"results":{id:{"ok":true,"value":<適用後の値(範囲外はクランプ済み)>} \| {"ok":false,"error":{...}}}}`。要求自体は成功(`ok:true`)で、不正な id/値は id ごとのエラーになり、有効な分だけ適用される |
| `invoke` | V→S | `{"control":"<button のid>","args":{...}}`。ボタン/アクション実行(対象は `control`。`id` は要求の通し番号なので使わない) |
| `subscribe` | V→S | `{"ids":["..."]\|"*","maxHz":10}`。指定コントロールの変化を `changed` イベントで受け取る(`readout` はセーバー側の変化で更新される) |
| `bye` | 双方向 | 正常終了通知。受け取った側は接続を閉じる |

### 4.3 イベント (`ev`)

| ev | 内容 |
|---|---|
| `changed` | `{"values":{id:値,...}}` セーバー側が変えた値(readout の更新、セーバー自身による値変更) |
| `manifestChanged` | `{"rev":N}` マニフェスト(選択肢など)が変わった。ビュワーは `manifest` を再取得する |
| `log` | `{"level":"info\|warn\|error","message":"..."}` セーバーからの診断メッセージ(任意) |

### 4.4 エラーコード

`unknownId` / `typeMismatch` / `readOnly` / `unsupported`(未実装の op・型) / `notReady`(初期化前) /
`badRequest`(JSON 不正・必須項目欠落) / `internal`。
**範囲外の値はエラーにせずクランプ**して適用値を返す(スライダー操作の連続入力でエラーを出さないため)。
`enum` の未知の値は `badRequest`。

## 5. マニフェスト

```jsonc
{
  "scrapi": "1.0",
  "rev": 1,
  "saver": {"id": "example.saver", "name": "Example Saver", "version": "1.2.3"},
  "capabilities": ["scrapi.pause", "scrapi.seed", "scrapi.restart", "viewport.resize"],
  "controls": [ /* ノード(§5.1)の木 */ ]
}
```

### 5.1 ノード共通プロパティ

| プロパティ | 必須 | 内容 |
|---|---|---|
| `id` | ○(全ノード) | セーバー内で一意。`[A-Za-z0-9_.-]`、`.` で名前空間を区切る。`scrapi.` 始まりは予約(§6) |
| `type` | ○ | §5.2 の型。未知の型は汎用の読み取り専用テキストとして表示してよい |
| `label` | 推奨 | 表示名 |
| `description` | 任意 | ツールチップ/補助テキスト |
| `default` | 任意 | 既定値(ビュワーの「既定に戻す」で使う) |
| `access` | 任意 | `rw`(既定) / `ro`。`readout` は常に `ro` |
| `apply` | 任意 | `live`(既定: 即時反映) / `restart`(`scrapi.restart` の `invoke` 後に反映) |
| `visibleWhen` / `enabledWhen` | 任意 | 条件式(§5.3)。偽ならビュワーは非表示/無効化する |

### 5.2 型(ウィジェット語彙)

| type | 値の型 | 固有プロパティ | 推奨ウィジェット |
|---|---|---|---|
| `group` | -(値なし) | `children[]`, `presentation`: `section`(既定)/`tab`/`collapsible`/`collapsed`(折りたたみ可で、最初は閉じている) | 見出し・タブ・折りたたみ |
| `enum` | 文字列(`value`) | `options:[{value,label,description?}]`, `presentation`: `dropdown`(既定)/`radio`/`list` | プルダウン・ラジオボタン・リスト |
| `flags` | 文字列の配列 | `options`(同上) | チェックボックス群 |
| `bool` | true/false | - | チェックボックス・トグル |
| `int` / `float` | 数値 | `min`, `max`, `step`, `unit`, `scale`: `linear`(既定)/`log`, `presentation`: `slider`(既定)/`knob`/`spinner` | スライダ・ノブ・スピン |
| `string` | 文字列 | `multiline`, `maxLength`, `pattern` | テキスト入力 |
| `color` | `"#RRGGBB"` / `"#RRGGBBAA"` | `alpha`(bool), `palette:["#..."]` | カラーパレット/ピッカー |
| `path` | 文字列 | `kind`: `file`/`folder`, `filters:[{label,pattern}]` | 参照ボタン付き入力 |
| `button` | -(`invoke` で実行) | `confirm`(実行前確認文言) | ボタン |
| `readout` | 任意(数値/文字列/真偽) | `format`: `text`(既定)/`number`/`gauge`/`bar`, `min`, `max`, `unit`, `multiline` | テキスト表示領域・ゲージ |

`enum` の選択肢がセーバーの状態で変わりうる場合は、`options` を更新して `manifestChanged` を送る。

### 5.3 条件式

```jsonc
{"id": "fg.effect", "eq": "FlagWave"}          // 値が等しい
{"id": "fg.effect", "in": ["FlagWave","Tilt"]}
{"all": [ ... ]}  {"any": [ ... ]}  {"not": { ... }}
```
`visibleWhen` の参照先は同一マニフェスト内のコントロール `id`。評価はビュワー側の値キャッシュで行い、
ラウンドトリップを要しない。

## 6. 標準(予約)コントロール `scrapi.*`

すべて任意実装。実装したものは `capabilities` に名前を列挙する。マニフェスト内では通常のノードとして現れる。

| id | 型 | 意味 |
|---|---|---|
| `scrapi.paused` | bool | 描画時間の進行を止める |
| `scrapi.timeScale` | float | 時間の進む速さ(1.0 = 等倍) |
| `scrapi.step` | button | 一時停止中に 1 フレームだけ進める |
| `scrapi.seed` | int | 乱数シード(同一シード→同一の見た目、を実装できるセーバーのみ) |
| `scrapi.restart` | button | 現在の設定(`apply:"restart"` を含む)で内部状態を初期化し直す |
| `scrapi.fps` | readout(number) | 現在のフレームレート |
| `scrapi.saveToConfig` | button | 現在の値をセーバー自身の設定(`config.ini` 等)へ保存する。既定では**セッション限定**で保存しない |

`viewport.resize` (capability): ビュワーが `viewportHwnd`(`hello` 応答)を直接 `SetWindowPos` してサイズ変更
しても、セーバーは論理解像度を保ったまま描画を追従させる。未対応のセーバーには、ビュワーは固定サイズで
表示する(リサイズ不可)。

## 7. セーバー側の実装要件

- **スレッド**: 転送の読み書きは専用スレッドでよいが、状態の変更は**描画スレッドのフレーム境界**でだけ
  行う。`set` の応答は適用後に返す。
- **ロバスト性**: 不正な JSON・未知の `op`・接続断でセーバーがクラッシュしてはならない。
  接続断後は SCRAPI を停止し、通常のプレビューとして動き続ける(親ウィンドウ消滅で終了)。
- **副作用の制限**: `apply` 済みの値はセッション限定(§6 `scrapi.saveToConfig` を除き永続化しない)。
  ファイル書き込みやシステム設定の変更を伴う操作は `button` として明示し、`confirm` を付ける。
- **`/s` への非干渉**: `/s`(本番のスクリーンセーバー実行)では SCRAPI の受付を一切開始しない。

## 8. バージョニング

- `scrapi` は `メジャー.マイナー`。マイナーは後方互換な追加(型・イベント・op・プロパティの追加)のみ。
  メジャーが違う場合、ビュワーは接続を拒否して標準プレビューのみにフォールバックする。
- `hello` でビュワーが提示した `apiVersion` より高いマイナーの機能をセーバーは使ってはならない。

## 9. 将来拡張の余地 (v1 では定義しない)

`vec2`/`rect` 型(座標・矩形のインタラクティブ指定)、`image` 型(セーバー→ビュワーへの
サムネイル/デバッグ画像の転送)、プリセット保存の標準化、セーバー→ビュワー方向の要求、
ビュワー内蔵描画(共有テクスチャ)による埋め込み以外の表示方式。
