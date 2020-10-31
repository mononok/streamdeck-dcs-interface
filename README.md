<img src="Images/DCS_Interface_Banner.png" width=400>

`DCS Interface` は Streamdeck 用のプラグインです。
lua UDPソケットを通して DCSからシミュレーション情報を受け取ったり、クリッカブルコックピットにコマンドを送信することができます。

- [概要](#概要)
  - [ドキュメント詳細](#ドキュメント詳細)
- [デモ操作](#デモ操作)
- [インストール方法](#インストール方法)
    - [ダウンロード](#ダウンロード)
    - [バージョンアップ](#バージョンアップ)
    - [初期設定](#初期設定)
    - [ビデオ映像による説明](#ビデオ映像による説明)
- [ソースコード](#ソースコード)
- [Build from source instructions](#build-from-source-instructions)

# 概要

`DCS Interface` はプラグインです。DCSのイベントを発生させるボタンやインターフェースを作成することができます。
通常は作成したボタン毎に３つの設定があります。

- DCS コマンド - ゲーム上で動作させたいボタン/スイッチを指定します(コックピット内の操作可能なオブジェクト)。
  - Streamdeck は、プッシュボタン、スイッチ、ダイアル、レバーなどの入力をサポートします。
- 画像設定 - DCS内部情報に従って、Streamdeckの画像を条件付き変更する関数を指定します。
  - 例: 警告灯、スイッチステータス
- 文字設定 - DCS内部情報に従って、Streamdeckのボタンタイトルを表示する関数を指定します。
  - 例: UFC文字表示、スクラッチパッド、ラジオ表示

複数のStreamdeckも、もちろんサポートします。

## ドキュメント詳細

より詳しい取り扱い説明書は下記にあります：[Settings Help Documentation](Sources/com.ctytler.dcs.sdPlugin/helpDocs/helpContents.md).

---

# デモ操作

![Stream Deck AV8BNA ODU Demo](Images/Streamdeck_AV8B_Demo.gif)

**Example of Settings to Display Master Caution Lamp:**

<img src="Images/Configuration_AV8B_Screenshot.jpg" width=600>

# インストール方法

### ダウンロード

- DCS plugin が動作するには、まず始めに [DCS-ExportScripts](https://github.com/s-d-a/DCS-ExportScripts)が必要です。これの操作説明書は [Wiki](https://github.com/s-d-a/DCS-ExportScripts/wiki) にあります。これは DCSゲーム本体と通信するバックエンドプログラムとなります。

- DCS Interface Streamdeck pluginをインストールするには、次のファイルをダウンロードしてダブルクリックしインストールする必要があります。 `com.ctytler.dcs.streamDeckPlugin` from [Releases](https://github.com/charlestytler/streamdeck-dcs-interface/releases).

- [Releases](https://github.com/charlestytler/streamdeck-dcs-interface/releases) にある `icon_library.zip` もダウンロードして使用することができます。


### バージョンアップ

もし既に前バージョンがインストールされているときは、新しいバージョンをインストールする前に、古いバージョンをアンインストールしてください。
その操作は、StreamDeck GUIの右下にある "その他のアクション..."ボタンをクリックしてから、DCS Interface pluginの "Uninstall" をクリックしてください。

#### インストール済みバージョン番号の確認方法

StreamDeckにインストールしてあるPluginのバージョン番号の確認方法は、"その他のアクション..."ボタンをクリックします。そこにはインストール済みPlugin毎に
制作者とバージョン番号がタイトルの下に表示されます。

### 初期設定

もし 'DCS Interface'とDCS-ExportScriptだけを使う予定なら([Ikarus](https://github.com/s-d-a/Ikarus)は使わない)
`DCS-ExportScript\Config.lua`　のファイルを下記のように変更します。
(`IkarusPort`の所の値を `1625`から`1725`に変えます)。

```
-- Ikarus a Glass Cockpit Software
ExportScript.Config.IkarusExport    = true         -- false for not use
ExportScript.Config.IkarusHost      = "127.0.0.1"  -- IP for Ikarus
ExportScript.Config.IkarusPort      = 1725         -- Port Ikarus (1625)
ExportScript.Config.IkarusSeparator = ":"
```

The export script is also capable of supporting both, instructions can be found in the [Settings Help Documentation - Enabling Both DCS Interface & Ikarus](Sources/com.ctytler.dcs.sdPlugin/helpDocs/helpContents.md#enabling-both-dcs-interface--ikarus).

### ビデオ映像による説明

インストールと設定の概要は下記のリンクでたどることができます。
[DCS Interface for Streamdeck Video Instructions](https://www.youtube.com/playlist?list=PLcYO7a2ywThz7nIT4CjRTn737ZM26aqDq)

# ソースコード

The Sources folder contains the source code of the plugin. The primary components are as follows:

- `Sources/DcsInterface/MyStreamDeckPlugin.{cpp,h}` - StreamDeck C++ API (based on the Elgato streamdeck-cpu example)
- `Sources/DcsInterface/StreamdeckContext.{cpp,h}` - Class which stores each visible Streamdeck button's settings
- `Sources/DcsInterface/DcsInterface.{cpp,h}` - Interface between plugin and DCS
- `Sources/Test/*.cpp` - Contains unit tests for above classes and helps demonstrate their function
- `Sources/com.ctytler.dcs.sdPlugin/proprtyinspector` - Contains html and javascript for handling user settings
- `Sources/com.ctytler.dcs.sdPlugin/manifest.json` - Configuration for the Stream Deck plugin

# Build from source instructions

A build script is included which will build both the C++ executable which handles the communcation with DCS as well as the package for the Stream Deck plugin: `build_plugin.bat`

You must call this file from the [Developer Command Prompt for VS](https://docs.microsoft.com/en-us/dotnet/framework/tools/developer-command-prompt-for-vs) in order for the Visual C++ target build step to work.

You may also need to install the Boost C++ library as it is used by the base Streamdeck SDK.
Current version was built with Visual Studio Community 2019 and Boost 1.55.0.
