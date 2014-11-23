### ALIST
The ALIST command will give you back a list of all the devices and values in you open-zwave network.
It is a usefull way for a client to grab a lot of data and parse it.
This command has no parameters

### SETNODE
With the SETNODE command, you can change specific values of a device.
The following things can be set with a SETNODE command:
	Name, Location, Level, Thermostat Setpoint, Polling, Wake-up Interval and Battery report

For example, to set the name of node 2 to "living room lamp 1"
```
SETNODE~2~Name=living room lamp 1
```
You can set multiple values for a single device in one SETNODE command. These are separated by "<>".
```
SETNODE~2~Name=living room lamp 1<>Location=living room
```
### ROOMLIST
Much like the ALIST, this is a list of locations in you zwave network.
If you set the location of a device, a room is automatically created.
Room structures in this program are central places to store temperature related information.
If you have multiple radiator thermostats in a single room, you can controll them all at once.
The roomlist command has no parameters.
It returns the list of rooms including current temperature and current setpoint for this room.

### ROOM
With a ROOM command the temperature for a room can be set to a certain level.
The Polymer client gives a nice example of the workings of this command.
You already have a current setpoint for a room if you use radiator thermostats.
By giving the
```
ROOM~PLUS~<location name>
```
command, where location name is one of the locations you entered for a device, you can up the setpoint by 0.5 degrees.
```
ROOM~MINUS~<location name>
```
lowers the setpoint by 0.5
The server waits for 15 seconds before sending any command to a device in the network.
This is done to collect all plusses and minusses such to not overflow the device with messages.

### SCENE
The SCENE command relates to all scene options in this program.
You can create a scene:
```
SCENE~CREATE~<scene name>
```
You can add values to a scene:
```
SCENE~ADD~<scene name>~<node id>~60
```
Please note that this tries to add the valueid that is mapped to the basic command class.
For switches, dimmers and thermostats, this works great.
I have not tested it for other devices...

You can remove values from a scene:
```
SCENE~REMOVE~<scene name>~<node id>
```
This will remove the node/value pair from the scene.

You can activate a scene:
```
SCENE~ACTIVATE~<scene name>
```
This will execute the scene and set all the proper values for the nodes.

### CONTROLLER
With a CONTROLLER command, you can add or remove devices from your open-zwave network.
To add a device to the network use:
```
CONTROLLER~ADD
```

To remove a device from the network use:
```
CONTROLLER~REMOVE
```

The above commands will lock the open-zwave functionality until completed.
If you want to cancel one of the above commands, send:
```
CONTROLLER~CANCEL
```

I didn't have any failed nodes yet, so it was not needed to implement it yet.
Maybe I will in the future.

### CRON
This command sets internal alarms.
You can schedule zcron.sh with cron.
zcron.sh will send this CRON command to the server at a set time each day.
The internal alarms can be used to trigger sunrise and sunset events.

### SWITCH
The SWITCH command tells the server you are either leaving or coming home.
It will invoke CRON if it is not run yet to know the current time of day and what scene to run when you get home.
It runs the dayScene (in config.ini) if it is between sunrise and sunset and it will run the nightScene (in config.ini) otherwise.
If you leave the house, it runs the awayScene.
Please note that when the server starts, it assumes you are away from home.

### POLLINTERVAL
With the POLLINTERVAL command, you can set the poll interval in minutes.
```
POLLINTERVAL~15
```
The default poll interval is set to 30 minutes.
Polling devices is off by default.
Enable polling for a device with the SETNODE command.

### ALARMLIST
The ALARMLIST command shows a list of the currently scheduled alarms and their time to go off.

### TEST
The TEST command is purely for development.
If you want to develop, it is easy to use this command and add some sample code to test a feature.
In the master branch of the repository, it is always empty and the command will simply do nothing.

### EXIT
The EXIT command closes all the socket connections (without warning) and stops the server.
This is the only clean way to stop the server.
CTRL^C will work, but is not as clean.
