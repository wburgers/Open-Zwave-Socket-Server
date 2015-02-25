/*
initialize some vars
*/
window.WebSocket = window.WebSocket || window.MozWebSocket;
var websocket;
var websocket_status;
var switchbutton;
var nodeInfo = [];
var Rooms = [];
var Scenes = [];

//polymer elements
var single_room = document.getElementById('single-room');;
var room_list = document.getElementById('room-list');
var scene_list = document.getElementById('scene-list');

document.addEventListener('polymer-ready', function() {
	var menu = document.querySelector('core-menu');
	var scene_list = document.getElementById('scene-list');
	websocket_status = document.getElementById('websocket-status');
	var sendinput = document.getElementById('sendinput');
	switchbutton = document.getElementById('switchbutton');

	menu.addEventListener('core-select', function() {
		room_list.show = menu.selected;
		single_room.show = menu.selected;
		scene_list.show = menu.selected;
	});

	document.addEventListener('change-view', function(e) {
		menu.selected = e.detail.view;
		single_room.roomName = e.detail.roomName;
		updateSingleRoom();
	});
	
	document.addEventListener('send-command', function(e) {
		websocket.send(e.detail.command);
	});
	
	sendinput.addEventListener('change', function() {
		websocket.send(sendinput.committedValue);
		sendinput.value = "";
	});
	
	switchbutton.addEventListener('tap', function() {
		switchbutton.colorChange();
		websocket.send("SWITCH");
	});
	
	room_list.show = menu.selected;
	single_room.show = menu.selected;
	scene_list.show = menu.selected;

	open_websocket();
});

function open_websocket() {
	websocket = new WebSocket('ws://'+server_ip+':'+port, 'open-zwave');
	try {
		websocket.onopen = function () {
			websocket_status.color = 'green';
			websocket_status.status = 'Connected';
			//Init();
			GetDeviceList();
		};
		websocket.onerror = function (error) {
			websocket_status.color = 'red';
			websocket_status.status = 'Error Check console';
			console.log("error: " + error.data);
		};
		websocket.onmessage = function (message) {
			console.log(message.data);
			var parsed = JSON.parse(message.data);
			var command = parsed.command;
			var commandSwitch = {
				"UPDATE": function() {
					GetDeviceList();
					//sleep(25, Refresh);
				},
				"ALIST": function() {
					single_room.Devices = parsed.nodes.slice();
				},
				"ROOMLIST": function() {
					room_list.Rooms = parsed.rooms.slice();
					updateSingleRoom();
				},
				"ROOM": function() {
					room_list.Rooms.forEach( function(roomitem) {
						if (roomitem.Name === parsed.room.Name) {
							roomitem.currentSetpoint = parsed.room.currentSetpoint;
							roomitem.currentTemp = parsed.room.currentTemp;
						}
					});
				},
				"SCENELIST": function() {
					scene_list.Scenes = parsed.scenes.slice();
				},
				"SWITCH": function() {
				},
				"ATHOME": function() {
					switchbutton.atHome = parsed.athome;
				},
				"default": function() {
				},
			};
			
			if ( commandSwitch[command] ) {
				commandSwitch[command]();
			}
			else {
				commandSwitch["default"]();
			}
		};
		websocket.onclose = function (event) {
			websocket_status.color = 'red';
			websocket_status.status = 'Closed, Check console';
			console.log("Closed with code: " + event.code + " " + event.reason);
		};
	}
	catch(exception) {
		alert('Error' + exception);
	}
};

function Init() {
}

function GetDeviceList() {
	websocket.send("ALIST");
	websocket.send("ROOMLIST");
	websocket.send("SCENELIST");
	websocket.send("ATHOME");
}

function updateSingleRoom() {
	if(typeof single_room.roomName != 'undefined' && single_room.roomName !== "") {
		room_list.Rooms.forEach( function(roomItem) {
			if (roomItem.Name === single_room.roomName) {
				single_room.room = roomItem;
			}
		});
	}
}