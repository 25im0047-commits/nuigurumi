# 通信パーサーのテスト

リポジトリのルートで、Java 17以上を使って実行します（macOS／Linuxのシェル例）。

```sh
check_dir=$(mktemp -d)
javac -encoding UTF-8 -d "$check_dir" processing/nuigurumi_monitor/MonitorProtocol.java processing/nuigurumi_monitor/tests/ProtocolTest.java
java -cp "$check_dir" ProtocolTest processing/nuigurumi_monitor/tests/firmware-log-fixtures.txt
```

フィクスチャは2026-10-14版Arduinoの実際のログ出力関数をホスト上で実行した6行です。実機UARTで取得したデータではありません。

旧形式のログ、途中の行、値の範囲、重複項目、矛盾した状態、壊れた2万行の入力を検証します。Arduinoの出力形式を変えた場合は、フィクスチャとテストも確認してください。
