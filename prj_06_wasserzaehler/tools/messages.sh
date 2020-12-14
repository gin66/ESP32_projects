#!/bin/sh

messages=`echo ~/Downloads/Telegram\ Desktop/ChatExport*/messages.html`

echo $messages

if [ ! -f "$messages" ]
then
	echo No messages to download: $messages
	exit
fi

gawk '
BEGIN{
	FS = "[\" ]"
}

/title/ {
	date_time = $(NF-2) " " $(NF-1)
}

/Result/{
	gsub("<.*\">","")
	gsub("</a>","")
	r = $NF
	gsub("/"," ",r)
	print(date_time " " r)
}
' "$messages"
