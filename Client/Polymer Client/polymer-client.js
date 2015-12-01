var app;
var websocketElement;
var websocketStatus;
var switchbutton;
var google_signin;
var google_signin_status;
var room_list;
var single_room;
var scene_list;
var sendinput;
var drawerPanel;

var commandSwitch;

(function (document) {
	'use strict';

	// Grab a reference to our auto-binding template
	// and give it some initial binding values
	// Learn more about auto-binding templates at http://goo.gl/Dx1u2g
	app = document.querySelector('#app');

	// Listen for template bound event to know when bindings
	// have resolved and content has been stamped to the page
	app.addEventListener('dom-change', function () {
		registerGlobals();
		registerEventListeners();
	});

	app.route = "rooms";

	// See https://github.com/Polymer/polymer/issues/1381
	window.addEventListener('WebComponentsReady', function () {
		document.querySelector('body').removeAttribute('unresolved');
		
		app.wsProtocols = 'open-zwave';
		var connection = '';
		if (secure) {
			connection += 'wss://';
		} else {
			connection += 'ws://';
		}
		connection += server_ip + ':' + port;
		app.wsURL = connection;
	});
})(document);

function registerGlobals() {
	websocketElement = document.querySelector('websocket-component');
	websocketStatus = document.querySelector('#websocket-status');
	switchbutton = document.querySelector('#switch-button');
	google_signin = document.getElementById('google-signin');
	google_signin.clientId = client_id;
	google_signin_status = document.getElementById('google-signin-status');
	room_list = document.getElementById('room-list');
	single_room = document.getElementById('single-room');
	scene_list = document.getElementById('scene-list');
	sendinput = document.getElementById('sendinput');
	drawerPanel = document.getElementById('paperDrawerPanel');
	
	commandSwitch = {
		"AUTH" : function (parsed) {
			if (parsed.auth === true) {
				google_signin_status.profile = JSON.parse(parsed.profile);
				google_signin_status.signedIn = true;
				GetDeviceList();
			}
		},
		"UPDATE" : function () {
			GetDeviceList();
		},
		"ALIST" : function (parsed) {
			single_room.Devices = parsed.nodes.slice();
		},
		"ROOMLIST" : function (parsed) {
			room_list.Rooms = parsed.rooms.slice();
			updateSingleRoom();
		},
		"ROOM" : function (parsed) {
			room_list.Rooms.forEach(function (roomItem) {
				if (roomItem.Name === parsed.room.Name) {
					roomItem.currentTemp = parsed.room.currentTemp;
					roomItem.currentSetpoint = parsed.room.currentSetpoint;
					updateSingleRoom();

				}
			});
		},
		"SCENELIST" : function (parsed) {
			scene_list.Scenes = parsed.scenes.slice();
		},
		"SWITCH" : function () {},
		"ATHOME" : function (parsed) {
			switchbutton.atHome = parsed.athome;
		},
		"default" : function () {},
	};
};

function registerEventListeners() {
	switchbutton.addEventListener('tap', function () {
		switchbutton.changeColor();
		websocketElement.send("SWITCH");
	});

	sendinput.addEventListener('change', function () {
		websocketElement.send(sendinput.value);
		sendinput.value = "";
	});

	document.addEventListener('send-command', function (e) {
		console.log("Sending command", e.detail.command);
		websocketElement.send(e.detail.command);
	});

	// Close drawer after menu item is selected if drawerPanel is narrow
	app.onMenuSelect = function () {
		if (drawerPanel.narrow) {
			drawerPanel.closeDrawer();
		}
	}

	document.addEventListener('change-view', function (e) {
		app.route = e.detail.view;
		single_room.roomName = e.detail.roomName;
		updateSingleRoom();
	});
	
	document.addEventListener('websocket-open', function() {
		console.log("WEBSOCKET", "opening websocket");
	});
	
	document.addEventListener('websocket-error', function(event) {
		console.log("error: " + event.detail.data);
	});
	
	document.addEventListener('websocket-close', function(event) {
		console.log("Closed with code: " + event.detail.code + " " + event.detail.reason);
	});
	
	document.addEventListener('websocket-message', function(event) {
		console.log(event.detail.data);
		var parsed = JSON.parse(event.detail.data);
		var command = parsed.command;
		if (commandSwitch[command]) {
			commandSwitch[command](parsed);
		} else {
			commandSwitch["default"]();
		}
	});
}

function GetDeviceList() {
	websocketElement.send("ALIST");
	websocketElement.send("ROOMLIST");
	websocketElement.send("SCENELIST");
	websocketElement.send("ATHOME");
}

function updateSingleRoom() {
	if (typeof single_room.roomName != 'undefined' && single_room.roomName !== "") {
		room_list.Rooms.forEach(function (roomItem) {
			if (roomItem.Name === single_room.roomName) {
				single_room.reassign(roomItem);
			}
		});
	}
}
