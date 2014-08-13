#!/bin/bash
exec {fd}<>"/dev/tcp/localhost/60004"
echo "CRON" >&$fd
head -n0 <&$fd
exec {fd}>&-
exec {fd}<&- 
