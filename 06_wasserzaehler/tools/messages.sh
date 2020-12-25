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
	$0 = r
	dm3 = $1/36
	cm3 = $2/36
	mm3 = $3/36
	ccm3 = $4/36

	if ($1 % 36 == 0) {
		stat_dm3_00_cm3[$2]++
	}
	else {
		stat_dm3_18_cm3[$2]++
	}

	printf("%s %-17s   %.0f/%.0f/%.2f/%.2f l\n",date_time,$0,dm3*100,cm3*10,mm3,ccm3*0.1)
}

END {
   for (k = 0;k < 360;k += 18) {
	printf("%3d: ",k)
	if (k in stat_dm3_00_cm3) {
		printf("%3d",stat_dm3_00_cm3[k])
	}
    else {
		printf("    ")
	}
	if (k in stat_dm3_18_cm3) {
		printf(" %3d",stat_dm3_18_cm3[k])
	}
    printf("\n")
   }
}
' "$messages"
