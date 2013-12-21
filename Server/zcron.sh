#!/bin/bash
exec {fd}<>"/dev/tcp/localhost/6004"
echo "CRON" >&$fd
head -n0 <&$fd
exec {fd}>&-
exec {fd}<&- 
