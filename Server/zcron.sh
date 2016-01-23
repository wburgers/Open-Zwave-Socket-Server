#!/bin/bash
exec {fd}<>"/dev/tcp/localhost/60004"
echo "CRON" >&$fd
head -n1 <&$fd > /dev/null
exec {fd}>&-
exec {fd}<&-