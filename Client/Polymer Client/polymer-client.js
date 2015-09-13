var app;
var websocket;
var websocketStatus;
var switchbutton;
var google_signin;
var google_signin_status;
var room_list;
var single_room;
var scene_list;
var sendinput;
var drawerPanel;

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
		openWebsocket();
	});

	app.route = "rooms";

	// See https://github.com/Polymer/polymer/issues/1381
	window.addEventListener('WebComponentsReady', function () {
		document.querySelector('body').removeAttribute('unresolved');
	});

	// Close drawer after menu item is selected if drawerPanel is narrow
	app.onMenuSelect = function () {};
})(document);

function registerGlobals() {
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
};

function registerEventListeners() {
	switchbutton.addEventListener('tap', function () {
		switchbutton.changeColor();
		websocket.send("SWITCH");
	});

	sendinput.addEventListener('change', function () {
		websocket.send(sendinput.value);
		sendinput.value = "";
	});

	document.addEventListener('send-command', function (e) {
		console.log("Sending command", e.detail.command);
		websocket.send(e.detail.command);
	});

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
}

function openWebsocket() {
	var connection = '';
	if (secure) {
		connection += 'wss://';
	} else {
		connection += 'ws://';
	}
	connection += server_ip + ':' + port;
	websocket = new WebSocket(connection, 'open-zwave');
	try {
		websocket.onopen = function () {
			console.log("WEBSOCKET", "opening websocket");
			websocketStatus.color = 'green';
			websocketStatus.status = 'Connected';
		};
		websocket.onerror = function (error) {
			websocketStatus.color = 'red';
			websocketStatus.status = 'Error Check console';
			console.log("error: " + error.data);
		};
		websocket.onmessage = function (message) {
			console.log(message.data);
			var parsed = JSON.parse(message.data);
			var command = parsed.command;
			var commandSwitch = {
				"AUTH" : function () {
					if (parsed.auth === true) {
						google_signin_status.profile = JSON.parse(parsed.profile);
						google_signin_status.signedIn = true;
						GetDeviceList();
					}
				},
				"UPDATE" : function () {
					GetDeviceList();
				},
				"ALIST" : function () {
					single_room.Devices = parsed.nodes.slice();
				},
				"ROOMLIST" : function () {
					room_list.Rooms = parsed.rooms.slice();
					updateSingleRoom();
				},
				"ROOM" : function () {
					room_list.Rooms.forEach(function (roomItem) {
						if (roomItem.Name === parsed.room.Name) {
							roomItem.currentTemp = parsed.room.currentTemp;
							roomItem.currentSetpoint = parsed.room.currentSetpoint;
							updateSingleRoom();

						}
					});
				},
				"SCENELIST" : function () {
					scene_list.Scenes = parsed.scenes.slice();
				},
				"SWITCH" : function () {},
				"ATHOME" : function () {
					switchbutton.atHome = parsed.athome;
				},
				"default" : function () {},
			};

			if (commandSwitch[command]) {
				commandSwitch[command]();
			} else {
				commandSwitch["default"]();
			}
		};
		websocket.onclose = function (event) {
			websocketStatus.color = 'red';
			websocketStatus.status = 'Closed, Check console';
			console.log("Closed with code: " + event.code + " " + event.reason);
		};
	} catch (exception) {
		alert('Error' + exception);
	}
};

function GetDeviceList() {
	websocket.send("ALIST");
	websocket.send("ROOMLIST");
	websocket.send("SCENELIST");
	websocket.send("ATHOME");
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
