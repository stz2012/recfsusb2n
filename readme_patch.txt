差分のみのアーカイブです。オリジナルに上書して下さい。


[変更点]
・recpt1ctlに対応
・チューニング完了判定の数値を変更（"pDev->DeMod_GetSequenceState() < 8"の8を9に）
・受信開始時安定化待ち処理追加
・httpサーバ機能移植(--http poronumber)
・tssplitter_liteを内臓(--sid n1,n2,.. number,all,hd,sd1,sd2,sd3,1seg,epg)
・受信開始前にウェイトを入れるオプションを追加(--wait n[1=100mSec])
