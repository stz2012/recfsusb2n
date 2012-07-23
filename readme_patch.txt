decoder.h tssplitter_lite.cpp tssplitter_lite.h Makefileの4ファイルをコピーしてtssplitter_lite.diffでパッチを当ててください。

このパッチで増えるオプションは、"--sid"・"--trim"の２つです。
"--sid"は、recpt1のそれに加えEPG出力に必要なpidを保存する"epg"が追加されています。
"--trim"は、受信の冒頭から指定数のパケットを大雑把に廃棄します。千単位ぐらいで指定してください。
