### Installation instructions:
First, install dependencies for libwebsockets, jsoncpp and libsocket:
```
sudo apt-get install build-essential cmake libssl-dev zlib1g-dev
```
Then, fully install [libwebsockets](http://github.com/warmcat/libwebsockets).
After the git clone, do:
```
cd libwebsockets
mkdir build
cd build
cmake -D LWS_WITHOUT_TESTAPPS=1 ..
make
sudo make install
cd
```

Next up is [jsoncpp](https://github.com/open-source-parsers/jsoncpp).
After the git clone, do:
```
cd jsoncpp
mkdir -p build/debug
cd build/debug
cmake -DCMAKE_BUILD_TYPE=debug -DJSONCPP_LIB_BUILD_STATIC=ON -DJSONCPP_LIB_BUILD_SHARED=ON -G "Unix Makefiles" ../..
make
sudo make install
cd
```

Finally, we are going to install [libsocket++](https://github.com/dermesser/libsocket)
After the git clone, do:
```
cd libsocket
cmake CMakeLists.txt
make
sudo make install
cd
```

After libwebsockets, jsoncpp and libsocket++ are completely installed, it is time to build open-zwave and OZSSW.
To compile open-zwave, you need libudev-dev, so:
```
sudo apt-get install libudev-dev
```
Then clone [open-zwave](https://github.com/OpenZWave/open-zwave) and install it:
```
git clone https://github.com/OpenZWave/open-zwave.git
cd open-zwave
make
sudo make install
cd
```
You can then clone this repo and make it:
```
git clone https://github.com/wburgers/Open-Zwave-Socket-Server.git
cd Open-Zwave-Socket-Server
cd Server
make
```

The exacutable is in the same Server folder.
Copy the config.ini-dist to config.ini and set the options you want.

I included a form of identification for the websocket port.
Please note that this is not included for the socket port and therefore it is not advised to expose the socket port to the Internet.
To use the identification method, install node and npm and run the node gapiwrapper.

```
npm install
node gapiwrapper.js
```

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
Also make sure the Clients folder is hosted by your webserver of choice.
The Polymer Client makes use of the gapiwrapper.js file.
Since Google does not really update their c++ api clients, I made a small wrapper in node to call google apis.
To run this wrapper, simply run the following command in a new terminal (preferably in screen or something similar).
```
node gapiwrapper.js
```