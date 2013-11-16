exec {fd}<>"/dev/tcp/localhost/6004"
echo "CRON" >&$fd
#cat <&$fd
head -n2 <&$fd
exec {fd}>&-
exec {fd}<&-
