### Installation instructions:
First, install dependencies for libwebsockets:
```
sudo apt-get install build-essential cmake libssl-dev zlib1g-dev
```
Then, fully install [libwebsockets](http://github.com/warmcat/libwebsockets).
After the git checkout, do:
```
cd libwebsockets
mkdir build
cd build
cmake -D LWS_WITHOUT_TESTAPPS=1 ..
make
sudo make install
```

After libwebsockets is completely installed, it is time to build open-zwave and OZSS
To compile open-zwave, you need libudev-dev, so:
```
sudo apt-get install libudev-dev
```
Then download [open-zwave](https://code.google.com/p/open-zwave/) and unpack it.
You can then clone this repo.
Place the server folder of the repo in the open-zwave folder as open-zwave/cpp/examples/server, for example with a mount bind.
```
mount --rbind /path/to/Open-Zwave-Socket-Server/Server /path/to/open-zwave/cpp/examples/server
```
From the /path/to/open-zwave/.../server folder call make. This will build both open-zwave and the Open Zwave Socket Server.
The exacutable is in the same server folder.
Copy the config.ini-dist to config.ini and set the options you want.
I run my server from the root folder of open-zwave.
```
cp /path/to/open-zwave/.../server/test /path/to/open-zwave/openzwave-server
/path/to/open-zwave/openzwave-server &
```
If you want to run it anywhere else, you have to specify the open-zwave config folder in main.cpp and rebuild the server or copy the config folder.

Additionally, you can schedule zcron.sh to be run at a specific time.
I run mine every day at 4 AM. This schedules my sunrise and sunset triggers.
(Based on the latitude and logitude in the config.ini file).
In the config.ini file, you can specify which scenes to run when such a trigger is activated.
At sunrise, the morningScene is run.
At sunset, the nightScene is run.

Furhtermore you can specify which scenes to run when you are gone or coming home.
The SWITCH command tells the server you are either going away or coming home.
It activates the awayScene when you leave and the dayScene or nightScene depending on the time of day when you come home.

Please note that the Scenes themselves should still be created. See the COMMANDLIST.md file on how to do that.
Scenes are saved automatically by open-zwave.

Finally, if you want to run the Polymer Client (recommended), please run a bower update in the Polymer Client folder after installing node, npm and bower.
Also make sure the Clients folder is hosted by your webserver of choice