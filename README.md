# v2_qmk_fw

くっつきー2のQMKキーボード実装です。

このリポジトリは`qmk_firmware/keyboards/cue2keys/` にコピーする、キーボード実装フォルダだけを持ちます。
`qmk compile` をこのリポジトリ単体で直接実行する前提ではありません。

開発中は[メタリポジトリ](https://github.com/cue2keys/v2_meta_build)を利用するのがおすすめです。
関連するスキーマファイルを同期できます。

https://github.com/cue2keys/v2_meta_build

更新した内容はGithub Actionsを使い、最終的にreleaseに追加されます。

## 初回セットアップ

- `git`
- `qmk`
- `rsync`

推奨の開発フローは、メタリポジトリで `git submodule update --init --recursive` を行い、`v2_flatcc_schema/vendor/flatcc` を用意したうえで root から `just build-local qmk` を実行することです。

それ以外の場合は、下記の手順でセットアップしてください。

```sh
mkdir -p ../.work
git clone --depth 1 https://github.com/dvidelabs/flatcc.git ../.work/vendor/flatcc
git config core.hooksPath .githooks
```

## ビルド

```sh
./tools/build-local.sh --keyboard cue2keys --keymap default
```

clean build

```sh
./tools/build-local.sh --clean --keyboard cue2keys --keymap default
```

書き込み

```sh
./tools/build-local.sh --qmk-action flash --keyboard cue2keys --keymap default
```
