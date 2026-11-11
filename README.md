# lcd-widget-gui

[![PayPalで開発を支援する](https://img.shields.io/badge/PayPal-Donate-0070BA?logo=paypal&logoColor=white&style=for-the-badge)](https://paypal.me/okako1124)

小型な **4行キャラクタLCD** 向けの、最小構成 GUI ライブラリのサンプル実装です。
C言語で「Widget ＋ クラス関数表（vtable風）」の継承を、**`malloc` なし・静的確保だけ**で組み立てます。
PC 上では、ターミナルに 20文字×4行のモック LCD を描画して動作を確認できます。

> Qiita 連載「小型LCD向けGUI設計」のコンパニオンコードです。記事の説明用に新規へ書き起こしたもので、特定製品のソースコードではありません。

## 特徴

- **依存ゼロ**。C11 標準ライブラリのみ。`malloc` / `calloc` / `realloc` / `free` は不使用（すべて静的確保）。
- 派生型の先頭に親型を**実体で埋め込む**「継承風」設計。先頭メンバの `offset 0` を `_Static_assert` でコンパイル時に固定（オブジェクト側8＋クラス側8の計16本）。
- **クラス関数表は `const`**（一般的なマイコンでは ROM 配置可）。各 Widget が RAM に持つのは `klass` ポインタ1本だけ。
- 入力（キー）／画面状態（`GuiPage`・`GuiWidget`）／描画（`GuiDisplayPort`）／装置制御（`DeviceControlPort`）を分離。
- 編集モデルは **Change / Submit / Cancel** の3本立て。
- 割り込みではキーを確定イベントへ変換するだけ。Widget 更新と LCD 転送はメインループ／GUIタスク側で行う前提。

## 型階層

オブジェクト側とクラス側で、同じかたちの先頭埋め込みを行います。

```text
GuiWidget                      GuiWidgetClass
├ GuiPage                      ├ GuiPageClass
└ GuiVisual                    └ GuiVisualClass
   ├ GuiLabel                     ├ GuiLabelClass
   └ GuiItem                      └ GuiItemClass
      ├ GuiButton                    ├ GuiButtonClass
      └ GuiNamedItem                 └ GuiNamedItemClass
         ├ GuiCombo                     ├ GuiComboClass
         └ GuiSpin                      └ GuiSpinClass
```

基底 `GuiWidget` は `klass` / `parent` / `type` / `flags` の4メンバ。`parent` は子から所有元へ戻る片方向参照で、子の一覧は `GuiPage.children` が持ちます。座標・サイズは基底ではなく `GuiVisual` から下だけが持ち、`GuiPage` は座標を持ちません（使わないメンバの RAM を全 Widget が払わないため）。

## ビルドと実行

```sh
make        # cc -std=c11 -Wall -Wextra -Wpedantic -o demo gui.c demo.c
make run    # ./demo
```

Make を使わない場合は、記事と同じコマンドで直接ビルドできます。

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -o demo gui.c demo.c
./demo
```

確認環境：gcc (Ubuntu) 13.3.0 / C11 / 警告 0 件 / 全シナリオの `assert` 成功（終了コード 0）。CI（GitHub Actions）でも同じビルドと実行を回しています。

## 出力例（ターミナルのモックLCD）

`./demo` は 20文字×4行のモック LCD を描画しながら、設計要件のシナリオを順に検証します。

```text
+--------------------+
|Device Settings     |
|>Rate:115k [Apply]  |
| Lvl:5     Port:P1  |
|                    |
+--------------------+
```

行頭の記号は `GuiIcon` をモック LCD が文字へ落としたものです。`>` がカーソル、`*` が編集中、`#` が操作不可。実機では、ここが LCD コントローラの外字番号になります。

## ファイル構成

| ファイル | 内容 |
| --- | --- |
| `gui.h` | Widget 階層、クラス関数表階層、`GuiDisplayPort`、各 Widget の API |
| `gui.c` | クラス関数表の実体、フォーカス移動、イベントディスパッチ、差分描画 |
| `demo.c` | 20文字×4行のモック LCD、モック DeviceControl、シナリオ検証 |
| `Makefile` | ビルド／実行／クリーン |
| `.github/workflows/ci.yml` | ビルド＋demo実行＋malloc系不使用チェック |

## demo が検証していること

| # | 確認内容 |
| --- | --- |
| 1 | Page 表示前に現在値が読み込まれる（`before_show`） |
| 2 | 初期フォーカスが正しい（`selected_child`） |
| 3 | 上下左右で指定したリンク先へ移動する |
| 4 | 非表示・無効な Item を読み飛ばす |
| 5 | Combo / Spin の Change でプレビューが呼ばれる（確定値は不変） |
| 6 | Submit で確定値が更新され、DeviceControl へ commit される |
| 7 | Cancel で編集開始前の値とハードウェア状態へ戻る |
| 8 | Button の execute が1回だけ呼ばれる |
| 9 | `GuiDisplayPort` をモックへ差し替えて描画できる |
| 10 | `malloc` 系関数を使っていない（ソース検索＋`nm -u`） |
| 11 | 各 child の `parent` が Page を指す（Page 自身は `parent == NULL`） |
| 12 | Page / Label / Button / Combo / Spin の `type` が正しい |
| 13 | 画面用クラス関数表が実行時に書き換えられていない（`static const`） |

## メモリサイズについて

`sizeof` はビルド環境で変わります。参考として x86-64 Linux では
`GuiWidget` 24 / `GuiVisual` 32 / `GuiItem` 96 / `GuiCombo` 152 / `GuiPage` 56 byte でした。
32bit MCU ではポインタ幅が違うため、ターゲットビルドで実測してください。

## 設計の解説（Qiita 連載）

背景や設計判断は連載記事で解説しています（公開後にリンクを追記）。

1. 4行LCDでもUI設計は必要だった ― 物理ボタンと専用ハードウェアをつなぐ
2. C言語で Widget と継承風設計を作る ― 4行LCD向け GUI の内部構造
3. malloc なしでイベント駆動 GUI を組み立てる
4. 物理ボタンから4行LCDの UI を設計する
5. UIイベントと専用ハードウェアを分離する
6. リアルタイムではない装置を、リアルタイムに感じさせる

## ライセンス

MIT License（[`LICENSE`](./LICENSE) 参照）。
