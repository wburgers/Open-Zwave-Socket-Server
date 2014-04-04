//-----------------------------------------------------------------------------
//
//	Main.cpp
//
//	Minimal application to test OpenZWave.
//
//	Creates an OpenZWave::Driver and the waits.  In Debug builds
//	you should see verbose logging to the console, which will
//	indicate that communications with the Z-Wave network are working.
//
//	Copyright (c) 2010 Mal Lansell <mal@openzwave.com>
//
//
//	SOFTWARE NOTICE AND LICENSE
//
//	This file is part of OpenZWave.
//
//	OpenZWave is free software: you can redistribute it and/or modify
//	it under the terms of the GNU Lesser General Public License as published
//	by the Free Software Foundation, either version 3 of the License,
//	or (at your option) any later version.
//
//	OpenZWave is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU Lesser General Public License for more details.
//
//	You should have received a copy of the GNU Lesser General Public License
//	along with OpenZWave.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------------
// Other modifications completed by conradvassallo.com, then thomasloughlin.com
// This version is by willemburgers.nl

//Open-Zwave includes:
#include <unistd.h>
#include <pthread.h>
#include "Options.h"
#include "Manager.h"
#include "Driver.h"
#include "Node.h"
#include "Scene.h"
#include "Group.h"
#include "Notification.h"
#include "ValueStore.h"
#include "Value.h"
#include "ValueBool.h"
#include "ValueByte.h"
#include "ValueDecimal.h"
#include "ValueInt.h"
#include "ValueList.h"
#include "ValueShort.h"
#include "ValueString.h"

//External classes and libs
#include "Socket.h"
#include "SocketException.h"
#include "ProtocolException.h"
#include "sunrise.h"
#include "Configuration.h"
#include <libwebsockets.h>

//Necessary includes for Main
#include <time.h>
#include <string>
#include <iostream>
#include <stdio.h>
#include <vector>
#include <stdlib.h>
#include <sstream>
#include <stdexcept>
#include <signal.h>

#include "Main.h"
using namespace OpenZWave;

//-----------------------------------------------------------------------------
// OpenZwave NodeInfo struct
//-----------------------------------------------------------------------------
typedef struct {
	uint32			m_homeId;
	uint8			m_nodeId;
	uint8			m_basicmapping;
	bool			m_needsSync;
	time_t			m_LastSeen;
	bool			m_polled;
	list<ValueID>	m_values;
} NodeInfo;

//-----------------------------------------------------------------------------
// Alarms in this Open-Zwave server have a time and a description
//-----------------------------------------------------------------------------
struct Alarm {
	time_t			alarmtime;
	std::string		description;
	bool operator<(Alarm const &other)  { return alarmtime < other.alarmtime; }
	bool operator==(Alarm const &other)  { return alarmtime == other.alarmtime; }
};

//-----------------------------------------------------------------------------
// LibWebSockets messages definitions
//-----------------------------------------------------------------------------
#define MAX_MESSAGE_QUEUE 32

static std::string ringbuffer[MAX_MESSAGE_QUEUE];
static int ringbuffer_head = 0;

static list<struct libwebsocket*> g_wsis;
struct libwebsocket_context *context;

/*struct WSClient {
	int id;
	float lat, lon;
	bool operator==(WSClient const &other) { return id == other.id;}
};*/

//static int nextClientID = 0;
//static bool notificationList_empty = true;
//static pthread_mutex_t g_notificationListMutex;

static bool stopping = false;
static Socket* server;
static Configuration* conf;

static uint32 g_homeId = 0;
static bool g_initFailed = false;
static bool atHome = true;
static bool alarmset = false;
static list<Alarm> alarmlist;
static list<NodeInfo*> g_nodes;
static pthread_mutex_t g_criticalSection;
static pthread_cond_t initCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

// Value-Defintions of the different String values
enum Commands {Undefined_command = 0, AList, SetNode, Room, Plus, Minus, SceneC, Create, Add, Remove, Activate, ControllerC, Cancel, Cron, Switch, PollInterval, AlarmList, Test, Exit};
enum Triggers {Undefined_trigger = 0, Sunrise, Sunset};
enum DeviceOptions {Undefined_Option = 0, Name, Location, Level, Thermostat_Setpoint, Polling, Wake_up_Interval, Battery_report};
static std::map<std::string, Commands> s_mapStringCommands;
static std::map<std::string, Triggers> s_mapStringTriggers;
static std::map<std::string, DeviceOptions> s_mapStringOptions;
static std::map<std::string, int> MapCommandClassBasic;
static std::map<std::string, float> RoomSetpoints;

void create_string_maps() {
	s_mapStringCommands["ALIST"] = AList;
	s_mapStringCommands["SETNODE"] = SetNode;
	s_mapStringCommands["ROOM"] = Room;
	s_mapStringCommands["PLUS"] = Plus;
	s_mapStringCommands["MINUS"] = Minus;
	s_mapStringCommands["SCENE"] = SceneC;
	s_mapStringCommands["CREATE"] = Create;
	s_mapStringCommands["ADD"] = Add;
	s_mapStringCommands["REMOVE"] = Remove;
	s_mapStringCommands["ACTIVATE"] = Activate;
	s_mapStringCommands["CONTROLLER"] = ControllerC;
	s_mapStringCommands["CANCEL"] = Cancel;
	s_mapStringCommands["CRON"] = Cron;
	s_mapStringCommands["SWITCH"] = Switch;
	s_mapStringCommands["POLLINTERVAL"] = PollInterval;
	s_mapStringCommands["ALARMLIST"] = AlarmList;
	s_mapStringCommands["TEST"] = Test;
	s_mapStringCommands["EXIT"] = Exit;
	
	s_mapStringTriggers["Sunrise"] = Sunrise;
	s_mapStringTriggers["Sunset"] = Sunset;
	
	s_mapStringOptions["Name"] = Name;
	s_mapStringOptions["Location"] = Location;
	s_mapStringOptions["Level"] = Level;
	s_mapStringOptions["Thermostat Setpoint"] = Thermostat_Setpoint;
	s_mapStringOptions["Polling"] = Polling;
	s_mapStringOptions["Wake-up Interval"] = Wake_up_Interval;
	s_mapStringOptions["Battery report"] = Battery_report;
	
	MapCommandClassBasic["0x03|0x11"] = 0x94;
	MapCommandClassBasic["0x03|0x12"] = 0x30;
	MapCommandClassBasic["0x08|0x02"] = 0x40;
	MapCommandClassBasic["0x08|0x03"] = 0x46;
	MapCommandClassBasic["0x08|0x04"] = 0x43;
	MapCommandClassBasic["0x08|0x05"] = 0x40;
	MapCommandClassBasic["0x08|0x06"] = 0x40;
	MapCommandClassBasic["0x09|0x01"] = 0x50;
	MapCommandClassBasic["0x10"] = 0x25;
	MapCommandClassBasic["0x11"] = 0x26;
	MapCommandClassBasic["0x12|0x01"] = 0x25;
	MapCommandClassBasic["0x12|0x02"] = 0x26;
	MapCommandClassBasic["0x12|0x03"] = 0x28;
	MapCommandClassBasic["0x12|0x04"] = 0x29;
	MapCommandClassBasic["0x13|0x01"] = 0x28;
	MapCommandClassBasic["0x13|0x02"] = 0x29;
	MapCommandClassBasic["0x16|0x01"] = 0x39;
	MapCommandClassBasic["0x20"] = 0x30;
	MapCommandClassBasic["0x21"] = 0x31;
	MapCommandClassBasic["0x30"] = 0x35;
	MapCommandClassBasic["0x31|0x01"] = 0x32;
	MapCommandClassBasic["0x40|0x01"] = 0x62;
	MapCommandClassBasic["0x40|0x02"] = 0x62;
	MapCommandClassBasic["0x40|0x03"] = 0x62;
	MapCommandClassBasic["0xa1"] = 0x71;
}

bool init_RoomSetpoints() {
	for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
		std::string location = Manager::Get()->GetNodeLocation(g_homeId, (*it)->m_nodeId);
		if(RoomSetpoints.find(location) == RoomSetpoints.end()) {
			float currentTemp=0.0;
			for(list<ValueID>::iterator vit = (*it)->m_values.begin(); vit != (*it)->m_values.end(); ++vit) {
				if(strcmp(Manager::Get()->GetValueLabel(*vit).c_str(), "Heating 1") !=0) {
					continue;
				}
				if(!Manager::Get()->GetValueAsFloat(*vit, &currentTemp)) {
					return false;
				}
			}
			RoomSetpoints.insert(std::pair<std::string, float>(location,currentTemp));
		}
	}
	return true;
}

//functions
void *websockets_main(void* arg);
void *run_socket(void* arg);
std::string process_commands(std::string data);
bool SetValue(int32 home, int32 node, std::string const value, uint8 cmdclass, std::string label, std::string& err_message);
std::string switchAtHome();
std::string activateScene(string sclabel);
bool parse_option(int32 home, int32 node, std::string name, std::string value, bool& save, std::string& err_message);
bool try_map_basic(int32 home, int32 node);
void sigalrm_handler(int sig);

//-----------------------------------------------------------------------------
// Common functions that can be used in every other function
//-----------------------------------------------------------------------------
void split(const string& s, const string& delimiter, vector<string>& v) {
	string::size_type i = 0;
	string::size_type j = s.find(delimiter);
	while (j != string::npos) {
		v.push_back(s.substr(i, j - i));
		i = j += delimiter.length();
		j = s.find(delimiter, j);
	}
	v.push_back(s.substr(i, s.length()));
}

string trim(string s) {
	return s.erase(s.find_last_not_of(" \n\r\t") + 1);
}

template <typename T>
T lexical_cast(const std::string& s) {
	std::stringstream ss(s);

	T result;
	if((ss >> result).fail() || !(ss >> std::ws).eof())
	{
		throw std::runtime_error("Bad cast");
	}

	return result;
}

//-----------------------------------------------------------------------------
// <GetNodeInfo>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------
NodeInfo* GetNodeInfo(uint32 const homeId, uint8 const nodeId) {
	for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
		NodeInfo* nodeInfo = *it;
		if((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId)) {
			return nodeInfo;
		}
	}

	return NULL;
}

NodeInfo* GetNodeInfo(Notification const* notification) {
	uint32 const homeId = notification->GetHomeId();
	uint8 const nodeId = notification->GetNodeId();
	return GetNodeInfo(homeId, nodeId);
}

//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------
void OnNotification(Notification const* _notification, void* _context) {
	// Must do this inside a critical section to avoid conflicts with the main thread
	pthread_mutex_lock(&g_criticalSection);

	switch(_notification->GetType()) {
		case Notification::Type_ValueAdded:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				// Add the new value to our list
				nodeInfo->m_values.push_back( _notification->GetValueID());
			}
			break;
		}

		case Notification::Type_ValueRemoved:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				// Remove the value from out list
				for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
					if((*it) == _notification->GetValueID()) {
						nodeInfo->m_values.erase(it);
						break;
					}
				}
			}
			break;
		}

		case Notification::Type_ValueChanged:
		{
			// One of the node values has changed
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_LastSeen = time( NULL );
				for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
                    if((*it) == _notification->GetValueID()) {
                        nodeInfo->m_values.erase(it);
                        break;
                    }
                }
				nodeInfo->m_values.push_back(_notification->GetValueID());
				
				//test for notifications
				std::string WSnotification = "Notification: ValueChanged";
				std::cout << "Adding notification to message list\n";
								
				ringbuffer[ringbuffer_head] = WSnotification;
				if (ringbuffer_head == (MAX_MESSAGE_QUEUE - 1)) {
					ringbuffer_head = 0;
				}
				else {
					ringbuffer_head++;
				}
				
				for(list<struct libwebsocket*>::iterator it = g_wsis.begin(); it != g_wsis.end(); ++it) {
					libwebsocket_callback_on_writable(context, (*it));
				}
			}
			break;
		}

		case Notification::Type_Group:
		{
			// One of the node's association groups has changed
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo = nodeInfo;            // placeholder for real action
			}
			break;
		}

		case Notification::Type_NodeAdded:
		{
			// Add the new node to our list
			NodeInfo* nodeInfo = new NodeInfo();
			nodeInfo->m_homeId = _notification->GetHomeId();
			nodeInfo->m_nodeId = _notification->GetNodeId();
			nodeInfo->m_polled = false;
			nodeInfo->m_needsSync = false;
			g_nodes.push_back(nodeInfo);
			break;
		}

		case Notification::Type_NodeRemoved:
		{
			// Remove the node from our list
			uint32 const homeId = _notification->GetHomeId();
			uint8 const nodeId = _notification->GetNodeId();
			for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
				NodeInfo* nodeInfo = *it;
				if((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId)) {
					g_nodes.erase(it);
					delete nodeInfo;
					break;
				}
			}
			break;
		}

		case Notification::Type_NodeEvent:
		{
			// We have received an event from the node, caused by a
			// basic_set or hail message.
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_LastSeen = time( NULL );
			}
			break;
		}

		case Notification::Type_PollingDisabled:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_polled = false;
			}
			break;
		}

		case Notification::Type_PollingEnabled:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_polled = true;
			}
			break;
		}

		case Notification::Type_DriverReady:
		{
			g_homeId = _notification->GetHomeId();
			break;
		}

		case Notification::Type_DriverFailed:
		{
			g_initFailed = true;
			pthread_cond_broadcast(&initCond);
			break;
		}

		case Notification::Type_AwakeNodesQueried:
		case Notification::Type_AllNodesQueried:
		case Notification::Type_AllNodesQueriedSomeDead:
		{
			pthread_cond_broadcast(&initCond);
			break;
		}

		case Notification::Type_NodeProtocolInfo:
		{
			char buffer[10];
			uint32 const homeId = _notification->GetHomeId();
			uint8 const nodeId = _notification->GetNodeId();
			if(NodeInfo* nodeInfo = GetNodeInfo(homeId, nodeId)) {
				
				uint8 generic = Manager::Get()->GetNodeGeneric(homeId , nodeId);
				uint8 specific = Manager::Get()->GetNodeSpecific(homeId, nodeId);
				
				snprintf(buffer, 10, "0x%02X|0x%02X", generic, specific);
				if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
					nodeInfo->m_basicmapping = MapCommandClassBasic[buffer];
				}
				else {
					// We didn't find a Generic+Specifc in the table, now we check
					// for Generic only
					snprintf(buffer, 10, "0x%02X", generic);

					// Check if we have a mapping in our map table
					if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
						nodeInfo->m_basicmapping = MapCommandClassBasic[buffer];
					}
				}
				
				nodeInfo->m_LastSeen = time(NULL);
			}
			break;
		}
		case Notification::Type_Notification:
			switch(_notification->GetNotification()) {
				case Notification::Code_Awake: {
					/*if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
						if(nodeInfo->m_needsSync) {
							time_t rawtime;
							tm * timeinfo;
							time(&rawtime);
							timeinfo=localtime(&rawtime);
							
							uint32 const homeId = _notification->GetHomeId();
							uint8 const nodeId = _notification->GetNodeId();
							std::string err_message = "";
							
							const std::string DAY[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
							stringstream ssHour, ssMin;
							ssHour << timeinfo->tm_hour;
							ssMin << timeinfo->tm_min;
							
							try {
								cout << "Day\n";
								if(!SetValue(homeId, nodeId, DAY[timeinfo->tm_wday], COMMAND_CLASS_CLOCK, "Day", err_message)) {
									cout << "Error in timesync (day) for node " << nodeId << ":\n" << err_message << endl;
								}
								cout << "Hour\n";
								cout << ssHour.str() << endl;
								if(!SetValue(homeId, nodeId, ssHour.str(), COMMAND_CLASS_CLOCK, "Hour", err_message)) {
									cout << "Error in timesync (hour) for node " << nodeId << ":\n" << err_message << endl;
								}
								cout << "Minute\n";
								if(!SetValue(homeId, nodeId, ssMin.str(), COMMAND_CLASS_CLOCK, "Minute", err_message)) {
									cout << "Error in timesync (min) for node " << nodeId << ":\n" << err_message << endl;
								}
							}
							catch (std::exception const& e) {
								std::cout << "Exception: " << e.what() << endl;
							}
							nodeInfo->m_needsSync = false;
						}
					}*/
				}
				default: {
				}
			}
		case Notification::Type_DriverReset:
		case Notification::Type_NodeNaming:
		case Notification::Type_NodeQueriesComplete: {
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_LastSeen = time(NULL);
			}
			break;
		}
		default: {
		}
	}

	pthread_mutex_unlock(&g_criticalSection);
}

void OnControllerUpdate( Driver::ControllerState cs, Driver::ControllerError err, void *ct ) {
	//m_structCtrl *ctrl = (m_structCtrl *)ct;

	// Possible ControllerState values:
	// ControllerState_Normal     - No command in progress.
	// ControllerState_Starting   - The command is starting.
	// ControllerState_Cancel     - The command was cancelled.
	// ControllerState_Error      - Command invocation had error(s) and was aborted.
	// ControllerState_Sleeping   - Controller command is on a sleep queue wait for device.
	// ControllerState_Waiting    - Controller is waiting for a user action.
	// ControllerState_InProgress - The controller is communicating with the other device to carry out the command.
	// ControllerState_Completed  - The command has completed successfully.
	// ControllerState_Failed     - The command has failed.
	// ControllerState_NodeOK     - Used only with ControllerCommand_HasNodeFailed to indicate that the controller thinks the node is OK.
	// ControllerState_NodeFailed - Used only with ControllerCommand_HasNodeFailed to indicate that the controller thinks the node has failed.

	pthread_mutex_lock( &g_criticalSection );

	switch (cs) {
		case Driver::ControllerState_Normal:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Normal - no command in progress", ctrl->m_homeId );
			std::cout << "ControllerState: Normal" << endl;
			//ctrl->m_controllerBusy = false;
			break;
		}
		case Driver::ControllerState_Starting:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Starting - the command is starting", ctrl->m_homeId );
			std::cout << "ControllerState: Starting" << endl;
			break;
		}
		case Driver::ControllerState_Cancel:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Cancel - the command was cancelled", ctrl->m_homeId );
			std::cout << "ControllerState: Cancel" << endl;
			break;
		}
		case Driver::ControllerState_Error:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Error - command invocation had error(s) and was aborted", ctrl->m_homeId );
			std::cout << "ControllerState: Error" << endl;
			break;
		}
		case Driver::ControllerState_Sleeping:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Sleeping - controller command is on a sleep queue wait for device", ctrl->m_homeId );
			std::cout << "ControllerState: Sleeping" << endl;
			break;
		}
		case Driver::ControllerState_Waiting:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Waiting - waiting for a user action", ctrl->m_homeId );
			std::cout << "ControllerState: Waiting" << endl;
			break;
		}
		case Driver::ControllerState_InProgress:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - InProgress - communicating with the other device", ctrl->m_homeId );
			std::cout << "ControllerState: InProgress" << endl;
			break;
		}
		case Driver::ControllerState_Completed:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Completed - command has completed successfully", ctrl->m_homeId );
			std::cout << "ControllerState: Completed" << endl;
			//ctrl->m_controllerBusy = false;
			break;
		}
		case Driver::ControllerState_Failed:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Failed - command has failed", ctrl->m_homeId );
			std::cout << "ControllerState: Failed" << endl;
			//ctrl->m_controllerBusy = false;
			break;
		}
		case Driver::ControllerState_NodeOK:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - NodeOK - the node is OK", ctrl->m_homeId );
			std::cout << "ControllerState: NodeOk" << endl;
			//ctrl->m_controllerBusy = false;

			// Store Node State

			break;
		}
		case Driver::ControllerState_NodeFailed:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - NodeFailed - the node has failed", ctrl->m_homeId );
			std::cout << "ControllerState: NodeFailed" << endl;
			//ctrl->m_controllerBusy = false;

			// Store Node State

			break;
		}
		default:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - unknown response", ctrl->m_homeId );
			std::cout << "ControllerState: Unknown" << endl;
			//ctrl->m_controllerBusy = false;
			break;
		}
	}

	// Additional possible error information
	switch (err) {
		case Driver::ControllerError_None:
		{
			//WriteLog( LogLevel_Debug, false, "Error=None" );
			break;
		}
		case Driver::ControllerError_ButtonNotFound:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Button Not Found" );
			std::cout << "ControllerError: Button Not Found" << endl;
			break;
		}
		case Driver::ControllerError_NodeNotFound:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Node Not Found" );
			std::cout << "ControllerError: Node Not Found" << endl;
			break;
		}
		case Driver::ControllerError_NotBridge:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Not a Bridge" );
			std::cout << "ControllerError: Not a Bridge" << endl;
			break;
		}
		case Driver::ControllerError_NotPrimary:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Not Primary Controller" );
			std::cout << "ControllerError: Not Primary Controller" << endl;
			break;
		}
		case Driver::ControllerError_IsPrimary:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Is Primary Controller" );
			std::cout << "ControllerError: Is Primary Controller" << endl;
			break;
		}
		case Driver::ControllerError_NotSUC:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Not Static Update Controller" );
			std::cout << "ControllerError: Not Static Update Controller" << endl;
			break;
		}
		case Driver::ControllerError_NotSecondary:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Not Secondary Controller" );
			std::cout << "ControllerError: Not Secondary Controller" << endl;
			break;
		}
		case Driver::ControllerError_NotFound:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Not Found" );
			std::cout << "ControllerError: Not Found" << endl;
			break;
		}
		case Driver::ControllerError_Busy:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Controller Busy" );
			std::cout << "ControllerError: Busy" << endl;
			break;
		}
		case Driver::ControllerError_Failed:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Failed" );
			std::cout << "ControllerError: Failed" << endl;
			break;
		}
		case Driver::ControllerError_Disabled:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Disabled" );
			std::cout << "ControllerError: Disabled" << endl;
			break;
		}
		case Driver::ControllerError_Overflow:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Overflow" );
			std::cout << "ControllerError: Overflow" << endl;
			break;
		}
		default:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Unknown error (%d)", err );
			std::cout << "ControllerError: Unknown error" << endl;
			break;
		}
	}

	pthread_mutex_unlock( &g_criticalSection );

	// If the controller isn't busy anymore and we still got something in the queue, fire off the command now
	/*if ( ctrl->m_controllerBusy == false )
	{
		if ( ! ctrl->m_cmd.empty() )
		{
			bool response;
			m_cmdItem cmd;

			cmd = ctrl->m_cmd.front();
			ctrl->m_cmd.pop_front();

			// Now start the BeginControllerCommand with the 2 supported options
			switch( cmd.m_command ) {
				case Driver::ControllerCommand_HasNodeFailed:
				{
					//ctrl->m_controllerBusy = true;
					//WriteLog( LogLevel_Debug, true, "DomoZWave_HasNodeFailed: HomeId=%d Node=%d (Queued)", ctrl->m_homeId, cmd.m_nodeId );
					response = Manager::Get()->BeginControllerCommand( ctrl->m_homeId, Driver::ControllerCommand_HasNodeFailed, OnControllerUpdate, ctrl, true, cmd.m_nodeId );
		                        //WriteLog( LogLevel_Debug, false, "Return=%s", (response)?"CommandSend":"ControllerBusy" );
					break;
				}
				case Driver::ControllerCommand_RequestNodeNeighborUpdate:
				{
					//ctrl->m_controllerBusy = true;
					//WriteLog( LogLevel_Debug, true, "DomoZWave_RequestNodeNeighborUpdate: HomeId=%d Node=%d (Queued)", ctrl->m_homeId, cmd.m_nodeId );
					response = Manager::Get()->BeginControllerCommand( ctrl->m_homeId, Driver::ControllerCommand_RequestNodeNeighborUpdate, OnControllerUpdate, ctrl, false, cmd.m_nodeId );
		                        //WriteLog( LogLevel_Debug, false, "Return=%s", (response)?"CommandSend":"ControllerBusy" );
					break;
				}
				default:
				{
					//WriteLog( LogLevel_Debug, true, "DomoZWave_OnControllerUpdate: HomeId=%d Node=%d (Queued)", ctrl->m_homeId, cmd.m_nodeId );
					//WriteLog( LogLevel_Debug, false, "ERROR: Invalid Command %d", cmd.m_command );
					break;
				}
			}
		}
	}*/
}

//-----------------------------------------------------------------------------
// Libwebsockets definitions
//-----------------------------------------------------------------------------

static int httpCallback(struct libwebsocket_context *context,
							struct libwebsocket *wsi,
							enum libwebsocket_callback_reasons reason,
							void *user, void *in, size_t len) {
	/* switch (reason) {
        // http://git.warmcat.com/cgi-bin/cgit/libwebsockets/tree/lib/libwebsockets.h#n260
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            printf("connection established\n");
           
        // http://git.warmcat.com/cgi-bin/cgit/libwebsockets/tree/lib/libwebsockets.h#n281
        case LWS_CALLBACK_HTTP: {
            char *requested_uri = (char *) in;
            printf("requested URI: %s\n", requested_uri);
           
            if (strcmp(requested_uri, "/") == 0) {
                std::string universal_response = "Hello, World!";
                // http://git.warmcat.com/cgi-bin/cgit/libwebsockets/tree/lib/libwebsockets.h#n597
                libwebsocket_write(wsi, (unsigned char *) universal_response.c_str(),
                                   universal_response.length(), LWS_WRITE_HTTP);

            } else {
                // try to get current working directory
                char cwd[1024];
                char *resource_path;
               
                if (getcwd(cwd, sizeof(cwd)) != NULL) {
                    // allocate enough memory for the resource path
                    resource_path = malloc(strlen(cwd) + strlen(requested_uri));
                   
                    // join current working direcotry to the resource path
                    sprintf(resource_path, "%s%s", cwd, requested_uri);
                    printf("resource path: %s\n", resource_path);
                   
                    char *extension = strrchr(resource_path, '.');
                    std::string mime;
                   
                    // choose mime type based on the file extension
                    if (extension == NULL) {
                        mime = "text/plain";
                    } else if (strcmp(extension, ".png") == 0) {
                        mime = "image/png";
                    } else if (strcmp(extension, ".jpg") == 0) {
                        mime = "image/jpg";
                    } else if (strcmp(extension, ".gif") == 0) {
                        mime = "image/gif";
                    } else if (strcmp(extension, ".html") == 0) {
                        mime = "text/html";
                    } else if (strcmp(extension, ".css") == 0) {
                        mime = "text/css";
                    } else {
                        mime = "text/plain";
                    }
                   
                    // by default non existing resources return code 400
                    // for more information how this function handles headers
                    // see it's source code
                    // http://git.warmcat.com/cgi-bin/cgit/libwebsockets/tree/lib/parsers.c#n1896
                    libwebsockets_serve_http_file(context, wsi, resource_path, mime.c_str(), NULL);
                   
                }
            }
           
            // close connection
            libwebsocket_close_and_free_session(context, wsi,
                                                LWS_CLOSE_STATUS_NORMAL);
            break;
        }
        default:
            printf("unhandled callback\n");
            break;
    } */
    return 0;
}

struct per_session_data__open_zwave {
	int ringbuffer_tail;
};

static int open_zwaveCallback(	struct libwebsocket_context *context,
								struct libwebsocket *wsi,
								enum libwebsocket_callback_reasons reason,
								void *user, void *in, size_t len) {
	uint n;
	struct per_session_data__open_zwave *pss = (struct per_session_data__open_zwave *)user;
	
	// reason for callback
	switch(reason) {
		case LWS_CALLBACK_ESTABLISHED:
		{
			pss->ringbuffer_tail = ringbuffer_head;
			printf("connection established\n");
			//g_wsis.push_back(wsi);
			break;
		}
		case LWS_CALLBACK_RECEIVE: {
			std::string command = (char*) in;
			vector<string> v;
			split(command, "~", v);
			std::string response = v[0] + "|";
			try {
				response += process_commands(command);
			}
			catch (ProtocolException& e) {
				string what = "ProtocolException: ";
				what += e.what();
				what += "\n";
				response = what;
			}
			catch (std::exception const& e) {
				std::cout << "Exception: " << e.what() << endl;
			}
			catch (SocketException& e) {
				std::cout << "SocketException: " << e.what() << endl;
			}

			// send the response
			libwebsocket_write(wsi, (unsigned char *)response.c_str(), response.length(), LWS_WRITE_TEXT);

			// log what we recieved.
			printf("received data: %s\n", (char*) in);
			break;
		}
		case LWS_CALLBACK_SERVER_WRITEABLE: {
			/*std::string clientID = "";
			stringstream ssClientID;
			ssClientID << nextClientID;
			clientID += ssClientID.str();
			libwebsocket_write(wsi, (unsigned char *)clientID.c_str(), clientID.length(), LWS_WRITE_TEXT);
			(++nextClientID)%100;
			std::cout << "sent clientID: " << clientID << endl;*/
			
			while (pss->ringbuffer_tail != ringbuffer_head) {
				n = libwebsocket_write(wsi, (unsigned char *)
					   ringbuffer[pss->ringbuffer_tail].c_str(),
					   ringbuffer[pss->ringbuffer_tail].length(),
									LWS_WRITE_TEXT);
				if (n < 0) {
					lwsl_err("ERROR %d writing to mirror socket\n", n);
					return -1;
				}
				if (n < ringbuffer[pss->ringbuffer_tail].length())
					lwsl_err("mirror partial write %d vs %d\n",
						   n, ringbuffer[pss->ringbuffer_tail].length());

				if (pss->ringbuffer_tail == (MAX_MESSAGE_QUEUE - 1))
					pss->ringbuffer_tail = 0;
				else
					pss->ringbuffer_tail++;

				if (((ringbuffer_head - pss->ringbuffer_tail) &
					  (MAX_MESSAGE_QUEUE - 1)) == (MAX_MESSAGE_QUEUE - 15))
					libwebsocket_rx_flow_allow_all_protocol(
							   libwebsockets_get_protocol(wsi));

				if (lws_send_pipe_choked(wsi)) {
					libwebsocket_callback_on_writable(context, wsi);
					break;
				}
				/*
				 * for tests with chrome on same machine as client and
				 * server, this is needed to stop chrome choking
				 */
				usleep(1);
			}
			break;
		}
		/*case LWS_CALLBACK_CLOSED: {
			for(list<struct libwebsocket*>::iterator it = g_wsis.begin(); it != g_wsis.end(); ++it) {
				if(wsi == (*it)) {
					std::cout << "remove wsi" << endl;
					g_wsis.erase(it);
					std::cout << "henk" << endl;
				}
			}
			break;
		}*/
		case LWS_CALLBACK_PROTOCOL_DESTROY: {
			std::cout << "open-zwave protocol not used anymore" << endl;
		}
		default:
		break;
	}
	return 0;
}

// protocol types for websockets
static struct libwebsocket_protocols protocols[] = {  
    {
        "http-only",
        httpCallback,
        0
    },
    {
        "open-zwave",
        open_zwaveCallback,
		sizeof(struct per_session_data__open_zwave),
        0
    },
    {
        NULL, NULL, 0
    }
};

/******** DOSTUFF() *********************
 There is a separate instance of this function 
 for each connection.  It handles all communication
 once a connnection has been established.
 *****************************************/

//-----------------------------------------------------------------------------
// <main>
// Create the driver and then wait
//-----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
	conf = new Configuration();
	pthread_mutexattr_t mutexattr;

	pthread_mutexattr_init(&mutexattr);
	pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&g_criticalSection, &mutexattr);
	pthread_mutexattr_destroy(&mutexattr);

	pthread_mutex_lock(&initMutex);

	// Create the OpenZWave Manager.
	// The first argument is the path to the config files (where the manufacturer_specific.xml file is located
	// The second argument is the path for saved Z-Wave network state and the log file. If you leave it NULL
	// the log file will appear in the program's working directory.
	//Options::Create("../../../../config/", "", "");
	Options::Create("./config/", "", "");
	Options::Get()->AddOptionInt("RetryTimeout", 5000);
	Options::Get()->Lock();

	Manager::Create();

    // Add a callback handler to the manager.  The second argument is a context that
    // is passed to the OnNotification method.  If the OnNotification is a method of
    // a class, the context would usually be a pointer to that class object, to
    // avoid the need for the notification handler to be a static.
    Manager::Get()->AddWatcher(OnNotification, NULL);

    // Add a Z-Wave Driver
    // Modify this line to set the correct serial port for your PC interface.

    string port = "/dev/ttyUSB0";

    Manager::Get()->AddDriver((argc > 1) ? argv[1] : port);
    //Manager::Get()->AddDriver( "HID Controller", Driver::ControllerInterface_Hid );

    // Now we just wait for the driver to become ready, and then write out the loaded config.
    // In a normal app, we would be handling notifications and building a UI for the user.
	
	Manager::Get()->SetPollInterval(1000*60*30, false); //default to 30 minutes
	
    pthread_cond_wait(&initCond, &initMutex);

    if(!g_initFailed) {
		create_string_maps();
		if(!init_RoomSetpoints()) {
			std::cerr << "Something went wrong configuring the Rooms";
			return 0;
		}
		Manager::Get()->WriteConfig(g_homeId);

		Driver::DriverData data;
		Manager::Get()->GetDriverStatistics(g_homeId, &data);

		printf("SOF: %d ACK Waiting: %d Read Aborts: %d Bad Checksums: %d\n", data.m_SOFCnt, data.m_ACKWaiting, data.m_readAborts, data.m_badChecksum);
		printf("Reads: %d Writes: %d CAN: %d NAK: %d ACK: %d Out of Frame: %d\n", data.m_readCnt, data.m_writeCnt, data.m_CANCnt, data.m_NAKCnt, data.m_ACKCnt, data.m_OOFCnt);
		printf("Dropped: %d Retries: %d\n", data.m_dropped, data.m_retries);
		printf("***************************************************** \n");
		
		//start the websocket in a new thread
		pthread_t websocket_thread;
		if(pthread_create(&websocket_thread , NULL ,  websockets_main ,NULL) < 0) {
			throw std::runtime_error("Unable to create thread");
		}
		else {
			std::cout<< "Websocket starting" << endl;
		}
		
		int port;
		
		if(!conf->GetTCPPort(port)) {
			std::cerr << "There is no TCP port set in Config.ini, please specify one and try again.\n";
			return 0;
		}
		std::cout << "Starting TCP server on port: " << port << endl;
		
		while(!stopping) {
			try { // for all socket errors
				server = new Socket();
				if(!server->create()) {
					throw SocketException ( "Could not create server socket." );
				}
				if(!server->bind(port)) {
					throw SocketException ( "Could not bind to port." );
				}
				if(!server->listen()) {
					throw SocketException ( "Could not listen to socket." );
				}
				Socket new_sock;
				while(server->accept(new_sock)) {
					pthread_t thread;
					int thread_sock2;
					thread_sock2 = new_sock.GetSock();
					if(pthread_create(&thread , NULL ,  run_socket ,(void*) thread_sock2) < 0) {
						throw std::runtime_error("Unable to create thread");
					}
					else {
						std::cout<< "Connection Created" << endl;
					}
				}
			}
			catch (SocketException& e) {
				std::cout << "SocketException: " << e.what() << endl;
			}
			catch(...) {
				std::cout << "Other exception" << endl;
			}
			std::cout << "Caught an exception, resolve the issue and press ENTER to continue" << endl;
			std::cin.ignore();
		}
    }
	
	// program exit (clean up)
	std::cout << "Closing connection to Zwave Controller" << endl;
	
	if(strcasecmp(port.c_str(), "usb") == 0) {
		Manager::Get()->RemoveDriver("HID Controller");
	}
	else {
		Manager::Get()->RemoveDriver(port);
	}
	Manager::Get()->RemoveWatcher(OnNotification, NULL);
	Manager::Destroy();
	Options::Destroy();
	pthread_mutex_destroy(&g_criticalSection);
	return 0;
}

void *websockets_main(void* arg) {
	int port;
	if(!conf->GetWSPort(port)) {
		std::cerr << "There is no WS port set in Config.ini, please specify one and try again.\n";
		return 0;
	}
	const char *interface = NULL;
	
	const char *cert_path =  NULL;//"./cpp/examples/server/cert/server.crt";
	const char *key_path = NULL;//"./cpp/examples/server/cert/server.key";

	// no special options
	int opts = 0;

	// create connection struct
	struct lws_context_creation_info info;
	info.port = port;
	info.iface = interface;
	info.protocols = protocols;
	info.extensions = NULL;
	info.ssl_cert_filepath = cert_path;
	info.ssl_private_key_filepath = key_path;
	info.options = opts;

	// create libwebsocket context representing this server
	context = libwebsocket_create_context(&info);

	// make sure it starts
	if(context == NULL) {
		std::cerr << "libwebsocket init failed\n";
		return 0;
	}
	std::cout << "starting websocket server...\n";

	// infinite loop, to end this server send SIGTERM. (CTRL+C)
	while (!stopping) {
		libwebsocket_service(context, 10);
	}
	libwebsocket_context_destroy(context);
	
	return 0;
}

void *run_socket(void* arg) {
	Socket thread_sock;
	thread_sock.SetSock((int)arg);
	while(true) {
		try { // command parsing errors
			//get commands from the socket
			std::string data;
			thread_sock >> data;
			
			if(strcmp(data.c_str(), "") == 0){ //client closed the connection
				std::cout << "Client closed the connection" << endl;
				return 0;
			}
			
			thread_sock << process_commands(data);
		}
		catch (ProtocolException& e) {
			string what = "ProtocolException: ";
			what += e.what();
			what += "\n";
			thread_sock << what;
		}
		catch (std::exception const& e) {
			std::cout << "Exception: " << e.what() << endl;
		}
		catch (SocketException& e) {
			std::cout << "SocketException: " << e.what() << endl;
		}
	}
}

std::string process_commands(std::string data) {
	vector<string> v;
	split(data, "~", v);
	string output = "";
	switch (s_mapStringCommands[trim(v[0].c_str())])
	{
		case AList:
		{
			string device;
			for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
				NodeInfo* nodeInfo = *it;
				int nodeID = nodeInfo->m_nodeId;
				
				string nodeType = Manager::Get()->GetNodeType(g_homeId, nodeInfo->m_nodeId);
				string nodeName = Manager::Get()->GetNodeName(g_homeId, nodeInfo->m_nodeId);
				string nodeZone = Manager::Get()->GetNodeLocation(g_homeId, nodeInfo->m_nodeId);
				string nodeValue ="";	//(string) Manager::Get()->RequestNodeState(g_homeId, nodeInfo->m_nodeId);
										//The point of this was to help me figure out what the node values looked like
				for(list<ValueID>::iterator vit = nodeInfo->m_values.begin(); vit != nodeInfo->m_values.end(); ++vit) {
					string tempstr="";
					Manager::Get()->GetValueAsString(*vit,&tempstr);
					tempstr= "="+tempstr;
					//hack to delimit values .. need to properly escape all values
					if(vit != nodeInfo->m_values.begin()) {
						nodeValue += "<>";
					}
					nodeValue += Manager::Get()->GetValueLabel(*vit) +tempstr;
				}

				if(nodeName.size() == 0)
					nodeName = "Undefined";

				if(nodeType != "Static PC Controller" && nodeType != "") {
					stringstream ssNodeName, ssNodeId, ssNodeType, ssNodeZone, ssNodeLastSeen, ssNodeValue;
					ssNodeName << nodeName;
					ssNodeId << nodeID;
					ssNodeType << nodeType;
					ssNodeZone << nodeZone;
					ssNodeLastSeen << trim(ctime(&(nodeInfo->m_LastSeen)));
					ssNodeValue << nodeValue;
					device += "DEVICE~" + ssNodeName.str() + "~" + ssNodeId.str() + "~"+ ssNodeZone.str() +"~" + ssNodeType.str() + "~" + ssNodeLastSeen.str() + "~" + ssNodeValue.str() + "#";
				}
			}
			device = device.substr(0, device.size() - 1) + "\n";                           
			std::cout << "Sent Device List \n";
			output += device;
			break;
		}
		case SetNode:
		{
			if(v.size() != 3) {
				throw ProtocolException(2, "Wrong number of arguments");
			}
			int Node = 0;
			string Options = "";
			
			Node = lexical_cast<int>(v[1].c_str());
			Options=trim(v[2].c_str());
			
			if(!Options.empty()) {
				vector<string> OptionList;
				split(Options, "<>", OptionList);
				bool save = false;
				
				for(std::vector<string>::iterator it = OptionList.begin(); it != OptionList.end(); ++it) {
					std::size_t found = (*it).find('=');
					if(found!=std::string::npos) {
						std::string name = (*it).substr(0,found);
						std::string value = (*it).substr(found+1);
						std::string err_message = "";
						if(!parse_option(g_homeId, Node, name, value, save, err_message)) {
							output += "Error while parsing options\n";
							output += err_message;
							break;
						}
					}
				}
				if(save) {
					//save details to XML
					Manager::Get()->WriteConfig(g_homeId);
				}
			}
			
			stringstream ssNode;
			ssNode << Node;
			output += "MSG~ZWave Name set Node=" + ssNode.str() + "\n";
			
			break;
		}
		case Room: //restructure this to prevent sending multiple messages
		{
			if(v.size() != 3) {
				throw ProtocolException(2, "Wrong number of arguments");
			}
			for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
				std::string location = Manager::Get()->GetNodeLocation(g_homeId, (*it)->m_nodeId);
				if(strcmp(location.c_str(), trim(v[2].c_str()).c_str()) != 0) {
					continue;
				}
				if(strcmp(Manager::Get()->GetNodeType(g_homeId, (*it)->m_nodeId).c_str(), "Setpoint Thermostat") !=0) {
					continue;
				}
				switch(s_mapStringCommands[trim(v[1].c_str())])
				{
					case Plus:
						RoomSetpoints[location] += 0.5;
						break;
					case Minus:
						RoomSetpoints[location] -= 0.5;
						break;
					default:
						throw ProtocolException(1, "Unknown Room command");
						break;
				}
				stringstream ssCurrentTemp;
				ssCurrentTemp << RoomSetpoints[location];
				uint8 cmdclass = COMMAND_CLASS_THERMOSTAT_SETPOINT;
				string err_message = "";
				if(!SetValue(g_homeId, (*it)->m_nodeId, ssCurrentTemp.str(), cmdclass, "Heating 1", err_message)) {
					output += err_message;
				}
			}
			break;
		}
		case SceneC:
		{
			if(v.size() < 3) {
				throw ProtocolException(2, "Wrong number of arguments");
			}
			switch(s_mapStringCommands[trim(v[1].c_str())])
			{
				case Create:
				{
					string sclabel = trim(v[2].c_str());
					if(int scid = Manager::Get()->CreateScene()) {
						stringstream ssID;
						ssID << scid;
						Manager::Get()->SetSceneLabel(scid, sclabel);
						output += "Scene created with name " + sclabel +" and scene_id " + ssID.str() + "\n";
					}
					Manager::Get()->WriteConfig(g_homeId);
					break;
				}
				case Add:
				{
					if(v.size() != 5) {
						throw ProtocolException(2, "Wrong number of arguments");
					}
					uint8 numscenes = 0;
					uint8 *sceneIds = new uint8[numscenes];
					
					if((numscenes = Manager::Get()->GetAllScenes(&sceneIds))==0) {
						throw ProtocolException(3, "No scenes created");
					}
					
					stringstream ssNum;
					ssNum << numscenes;
					output += "numscenes " + ssNum.str() + "\n";
					
					string sclabel = trim(v[2].c_str());
					int scid=0;
					int Node = lexical_cast<int>(v[3].c_str());
					double value = lexical_cast<double>(v[4].c_str());
					bool response;
					
					for(int i=0; i<numscenes; ++i) {
						scid = sceneIds[i];
						if(sclabel != Manager::Get()->GetSceneLabel(scid)) {
							continue;
						}
						output += "Found right scene\n";
						NodeInfo* nodeInfo = GetNodeInfo(g_homeId, Node);
						uint8 cmdclass = 0;
						if(nodeInfo->m_basicmapping > 0 || try_map_basic(g_homeId, Node)) {
							cmdclass = nodeInfo->m_basicmapping;
							std::cout << "mapped to " << (int) cmdclass << endl;
						}
						else {
							cmdclass = COMMAND_CLASS_BASIC;
							std::cout << "mapped to BASIC" << endl;
						}
						for(list<ValueID>::iterator vit = nodeInfo->m_values.begin(); vit != nodeInfo->m_values.end(); ++vit) {
							if((*vit).GetCommandClassId() == cmdclass) {
								// It works fine, EXCEPT for MULTILEVEL, then we need to ignore all except the first one
								if((*vit).GetCommandClassId() == COMMAND_CLASS_SWITCH_MULTILEVEL) {
									if((*vit).GetIndex() != 0) {
										continue;
									}
								}
								
								switch((*vit).GetType()) {
									case ValueID::ValueType_Bool: {
										bool bool_value;
										bool_value = (bool)value;
										response = Manager::Get()->AddSceneValue(scid, (*vit), bool_value);
										break;
									}
									case ValueID::ValueType_Byte: {
										uint8 uint8_value;
										uint8_value = (uint8)value;
										response = Manager::Get()->AddSceneValue(scid, (*vit), uint8_value);
										break;
									}
									case ValueID::ValueType_Short: {
										uint16 uint16_value;
										uint16_value = (uint16)value;
										response = Manager::Get()->AddSceneValue(scid, (*vit), uint16_value);
										break;
									}
									case ValueID::ValueType_Int: {
										int int_value;
										int_value = value;
										response = Manager::Get()->AddSceneValue(scid, (*vit), int_value);
										break;
									}
									case ValueID::ValueType_Decimal: {
										float float_value;
										float_value = (float)value;
										response = Manager::Get()->AddSceneValue(scid, (*vit), float_value);
										break;
									}
									case ValueID::ValueType_List: {
										response = Manager::Get()->AddSceneValue(scid, (*vit), (int)value);
										break;
									}
									default:
										output += "unknown ValueType";
										break;
								}
								
								if(!response)
									output+= "Something went wrong\n";
								else
									output += "Add valueid/value to scene\n";
							}
						}
					}
					Manager::Get()->WriteConfig(g_homeId);
					break;
				}
				case Remove:
				{
					if(v.size() != 4) {
						throw ProtocolException(2, "Wrong number of arguments");
					}
					uint8 numscenes = 0;
					uint8 *sceneIds = new uint8[numscenes];
					
					if((numscenes = Manager::Get()->GetAllScenes(&sceneIds))==0) {
						throw ProtocolException(3, "No scenes created");
					}
					
					stringstream ssNum;
					ssNum << numscenes;
					output += "numscenes " + ssNum.str() + "\n";
					
					string sclabel = trim(v[2].c_str());
					int scid=0;
					int Node = lexical_cast<int>(v[3].c_str());
					
					for(int i=0; i<numscenes; ++i){
						scid = sceneIds[i];
						
						if(sclabel != Manager::Get()->GetSceneLabel(scid)){
							continue;
						}
						output += "Found right scene\n";
						NodeInfo* nodeInfo = GetNodeInfo(g_homeId, Node);
						uint8 cmdclass = 0;
						if(nodeInfo->m_basicmapping > 0 || try_map_basic(g_homeId, Node)) {
							cmdclass = nodeInfo->m_basicmapping;
							std::cout << "mapped to " << (int) cmdclass << endl;
						}
						else {
							cmdclass = COMMAND_CLASS_BASIC;
							std::cout << "mapped to BASIC" << endl;
						}
						for(list<ValueID>::iterator vit = nodeInfo->m_values.begin(); vit != nodeInfo->m_values.end(); ++vit) {
							if((*vit).GetCommandClassId() == cmdclass) {
								// It works fine, EXCEPT for MULTILEVEL, then we need to ignore all except the first one
								if((*vit).GetCommandClassId() == COMMAND_CLASS_SWITCH_MULTILEVEL) {
									if((*vit).GetIndex() != 0) {
										continue;
									}
								}
								output += "Remove valueid from scene\n";
								Manager::Get()->RemoveSceneValue(scid, (*vit));
							}
						}
					}
					Manager::Get()->WriteConfig(g_homeId);
					break;
				}
				case Activate:
				{
					output += activateScene(v[2].c_str());
					break;
				}
				default:
					throw ProtocolException(1, "Unknown Scene command");
					break;
			}
			break;
		}
		case ControllerC:
		{
			switch(s_mapStringCommands[trim(v[1].c_str())])
			{
				case Add: {
					string response = "false";
					if(Manager::Get()->BeginControllerCommand( g_homeId, Driver::ControllerCommand_AddDevice, OnControllerUpdate))
						response = "true";
					output += response;
					break;
				}
				case Remove:
				case Cancel:
				default:
					break;
			}
		}
		case Cron:
		{
			//planning to add a google calendar add-in here.
			
			//set the daily alarms and check if any have passed already (I execute zcron.sh around 4:15 AM)
			time_t sunrise = 0, sunset = 0;
			float lat, lon;
			conf->GetLocation(lat, lon);
			if(GetSunriseSunset(sunrise,sunset,lat,lon)) {
				Alarm sunriseAlarm;
				Alarm sunsetAlarm;
				sunriseAlarm.alarmtime = sunrise;
				sunsetAlarm.alarmtime = sunset;
				sunriseAlarm.description = "Sunrise";
				sunsetAlarm.description = "Sunset";
				
				alarmlist.push_back(sunriseAlarm);
				alarmlist.push_back(sunsetAlarm);
			}
			
			alarmlist.sort(); // sort by timestamp
			alarmlist.unique(); // remove double timestamps
			
			time_t now = time(NULL);
			
			while(!alarmlist.empty() && (alarmlist.front().alarmtime) <= now)
			{alarmlist.pop_front();}
			
			if(!alarmlist.empty() && !alarmset && (alarmlist.front().alarmtime > now)) {
				signal(SIGALRM, sigalrm_handler);   
				alarm(alarmlist.front().alarmtime - now);
				alarmset = true;
			}
			
			//synchronize devices with Command_Class_Clock
			for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
				time_t rawtime;
				tm * timeinfo;
				time(&rawtime);
				timeinfo=localtime(&rawtime);
				
				for(list<ValueID>::iterator vit = (*it)->m_values.begin(); vit != (*it)->m_values.end(); ++vit) {
					if((*vit).GetCommandClassId() != COMMAND_CLASS_CLOCK) {
						continue;
					}
					
					switch((*vit).GetIndex()) {
						case 0: {
							std::string deviceDayValue;
							const std::string DAY[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
							if(Manager::Get()->GetValueListSelection((*vit), &deviceDayValue)) {
								if(strcmp(DAY[(timeinfo->tm_wday)].c_str(), deviceDayValue.c_str()) != 0) {
									(*it)->m_needsSync = true;
								}
							}
							else {
								stringstream ssNodeId;
								ssNodeId << (*it)->m_nodeId;
								output += "Could not get the day out of node " + ssNodeId.str() + "\n";
							}
							break;
						}
						case 1: {
							uint8 deviceHourValue = -1;
							if(Manager::Get()->GetValueAsByte((*vit), &deviceHourValue)) {
								if((int)deviceHourValue != timeinfo->tm_hour) {
									(*it)->m_needsSync = true;
								}
							}
							else {
								stringstream ssNodeId;
								ssNodeId << (*it)->m_nodeId;
								output += "Could not get the hour out of node " + ssNodeId.str() + "\n";
							}
							break;
						}
						case 2: {
							uint8 deviceMinuteValue = -1;
							if(Manager::Get()->GetValueAsByte((*vit), &deviceMinuteValue)) {
								if((int)deviceMinuteValue != timeinfo->tm_min) {
									(*it)->m_needsSync = true;
								}
							}
							else {
								stringstream ssNodeId;
								ssNodeId << (*it)->m_nodeId;
								output += "Could not get the minute out of node " + ssNodeId.str() + "\n";
							}
							break;
						}
						default:
							output += "could find the current time of node\n";
					}
				}
				//cout << (*it)->m_needsSync << endl;
			}
			break;
		}
		case Switch:
		{
			output += switchAtHome();
			break;
		}
		case PollInterval:
		{
			if(v.size() != 2) {
				throw ProtocolException(2, "Wrong number of arguments");
			}
			int interval = lexical_cast<int>(v[1].c_str()); //get the interval in minutes
			Manager::Get()->SetPollInterval(1000*60*interval, false);
		}
		case AlarmList:
			for(list<Alarm>::iterator it = alarmlist.begin(); it!=alarmlist.end(); it++) {
				stringstream ssTime;
				ssTime << ctime(&(it->alarmtime));
				output += ssTime.str();
			}
			break;
		case Test:
		{
			break;
		}
		case Exit:
			stopping = true;
			delete server;
			break;
		default:
			throw ProtocolException(1, "Unknown command");
			break;
	}
	return output;
}

bool SetValue(int32 home, int32 node, std::string const value, uint8 cmdclass, std::string label, std::string& err_message) {
	err_message = "";
	bool response;
	bool cmdfound = false;
	
	if(NodeInfo* nodeInfo = GetNodeInfo(home, node)) {
		// Find the correct instance
		for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
			int id = (*it).GetCommandClassId();
			if(id != cmdclass) {
				continue;
			}

			if(label != Manager::Get()->GetValueLabel(*it)) {
				continue;
			}
			
			switch((*it).GetType()) {
				case ValueID::ValueType_Bool: {
					response = Manager::Get()->SetValue(*it, lexical_cast<bool>(value));
					cmdfound = true;
					break;
				}
				case ValueID::ValueType_Byte: {
					response = Manager::Get()->SetValue(*it, (uint8) lexical_cast<int>(value));
					cmdfound = true;
					break;
				}
				case ValueID::ValueType_Short: {
					response = Manager::Get()->SetValue(*it, (uint16) lexical_cast<int>(value));
					cmdfound = true;
					break;
				}
				case ValueID::ValueType_Int: {
					response = Manager::Get()->SetValue(*it, lexical_cast<int>(value));
					cmdfound = true;
					break;
				}
				case ValueID::ValueType_Decimal: {
					response = Manager::Get()->SetValue(*it, lexical_cast<float>(value));
					cmdfound = true;
					break;
				}
				case ValueID::ValueType_List: {
					response = Manager::Get()->SetValueListSelection(*it, value);
					cmdfound = true;
					break;
				}
				default:
					err_message += "unknown ValueType | ";
					return false;
					break;
			}
		}

		if(!cmdfound) {
			err_message += "Couldn't match node to the required COMMAND_CLASS_SWITCH_BINARY or COMMAND_CLASS_SWITCH_MULTILEVEL\n";
			return false;
		}
	}
	else {
		//WriteLog( LogLevel_Debug, false, "Return=false (node doesn't exist)" );
		err_message += "node doesn't exist";
		response = false;
	}

	return response;
}

std::string switchAtHome() {
	time_t sunrise = 0, sunset = 0;
	float lat, lon;
	std::string output = "";
	if(!conf->GetLocation(lat, lon)) {
		output += "Could not get the location from Config.ini\n";
		return output;
	}
	if(GetSunriseSunset(sunrise,sunset,lat,lon)) {
		atHome = !atHome;
		if(atHome) {
			output += "Welcome home\n";
			time_t now = time(NULL);
			if(now > sunrise && now < sunset) {
				// turn on the athome scene set by the user for the day
				std::string dayScene;
				conf->GetDayScene(dayScene);
				try {
					output += activateScene(dayScene);
				}
				catch (ProtocolException& e) {
					std::string what = "ProtocolException: ";
					what += e.what();
					output += what + "\n";
					output += "No dayScene is set, set it in Config.ini\n";
				}
			}
			else {
				// turn on the athome scene set by the user for the evening/night
				std::string nightScene;
				conf->GetNightScene(nightScene);
				try {
					output += activateScene(nightScene);
				}
				catch (ProtocolException& e) {
					std::string what = "ProtocolException: ";
					what += e.what();
					output += what + "\n";
					output += "No nightScene is set, set it in Config.ini\n";
				}
			}
		}
		else {
			output += "Bye bye\n";
			std::string awayScene;
			conf->GetAwayScene(awayScene);
			try {
				output += activateScene(awayScene);
			}
			catch (ProtocolException& e) {
				string what = "ProtocolException: ";
				what += e.what();
				output += what + "\n";
				output += "No awayScene is set, set it in Config.ini\n";
			}
		}
	}
	return output;
}

std::string activateScene(std::string sclabel) {
	uint8 numscenes = 0;
	uint8 *sceneIds = new uint8[numscenes];
	
	sclabel = trim(sclabel);
	
	if((numscenes = Manager::Get()->GetAllScenes(&sceneIds))==0) {
		throw ProtocolException(3, "No scenes created");
	}
	
	int scid=0;
	
	for(int i=0; i<numscenes; ++i) {
		scid = sceneIds[i];
		if(sclabel != Manager::Get()->GetSceneLabel(scid)){
			continue;
		}
		Manager::Get()->ActivateScene(scid);
		return "Activate scene "+sclabel+"\n";
	}
	throw ProtocolException(4, "Scene not found");
}

bool parse_option(int32 home, int32 node, std::string name, std::string value, bool& save, std::string& err_message) {
	err_message = "";
	switch(s_mapStringOptions[name])
	{
		case Name:
		{
			pthread_mutex_lock(&g_criticalSection);
			Manager::Get()->SetNodeName(home, node, value);
			pthread_mutex_unlock(&g_criticalSection);
			save = true;
			return true;
			break;
		}
		case Location:
		{
			pthread_mutex_lock(&g_criticalSection);
			Manager::Get()->SetNodeLocation(home, node, value);
			pthread_mutex_unlock(&g_criticalSection);
			save = true;
			return true;
			break;
		}
		case Level:
		{
			uint8 cmdclass = COMMAND_CLASS_SWITCH_MULTILEVEL;
			return SetValue(home, node, value, cmdclass, "Level", err_message);
			break;
		}
		case Thermostat_Setpoint: {
			uint8 cmdclass = COMMAND_CLASS_THERMOSTAT_SETPOINT;
			return SetValue(home, node, value, cmdclass, "Heating 1", err_message);
			break;
		}
		case Polling:
		{
			bool found = false;
			if(Manager::Get()->GetNodeBasic( home, node ) < 0x03) {
				err_message += "Node is a controller\n";
				return false;
			}
			if(NodeInfo* nodeInfo = GetNodeInfo(home, node)) {
				uint8 cmdclass = 0;
				if(nodeInfo->m_basicmapping > 0 || try_map_basic(home, node)) {
					cmdclass = nodeInfo->m_basicmapping;
					std::cout << "mapped to " << (int) cmdclass << endl;
				}
				else {
					cmdclass = COMMAND_CLASS_BASIC;
					std::cout << "mapped to BASIC" << endl;
				}
				
				// Mark the basic command class values for polling
				for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it){
					if((*it).GetCommandClassId() == cmdclass) {
						// It works fine, EXCEPT for MULTILEVEL, then we need to ignore all except the first one
						if((*it).GetCommandClassId() == COMMAND_CLASS_SWITCH_MULTILEVEL) {
							if((*it).GetIndex() != 0) {
								continue;
							}
						}
						if(lexical_cast<int>(value) == 1) {
							if(!Manager::Get()->EnablePoll(*it)) {
								err_message += "Could not enable polling for this value\n";
								return false;
							}
						}
						else if(lexical_cast<int>(value) >= 2) {
							if(!Manager::Get()->EnablePoll(*it, 2)) {
								err_message += "Could not enable polling for this value\n";
								return false;
							}
						}
						else {
							if(!Manager::Get()->DisablePoll(*it)) {
								err_message += "Could not disable polling for this value\n";
								return false;
							}
						}
						if(!found)
						{
							found = true;
						}
					}
				}
				if(!found) {
					err_message += "Node does not have COMMAND_CLASS_BASIC\n";
					return false;
				}
			}
			save = true;
			return true;
			break;
		}
		case Wake_up_Interval:
		{
			uint8 cmdclass = COMMAND_CLASS_WAKE_UP;
			save = true;
			return SetValue(home, node, value, cmdclass, name, err_message);
		}
		case Battery_report:
		{
			uint8 cmdclass = COMMAND_CLASS_CONFIGURATION;
			save = true;
			return SetValue(home, node, value, cmdclass, "Send unsolicited battery report on wakeup", err_message);
		}
		default:
			break;
	}
	return false;
}

bool try_map_basic(int32 home, int32 node) {
	char buffer[10];
	if(NodeInfo* nodeInfo = GetNodeInfo(home, node)) {					
		uint8 generic = Manager::Get()->GetNodeGeneric(home, node);
		uint8 specific = Manager::Get()->GetNodeSpecific(home, node);			
		
		snprintf(buffer, 10, "0x%02X|0x%02X", generic, specific);
		if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
			nodeInfo->m_basicmapping = MapCommandClassBasic[buffer];
			return true;
		}
		else {
			// We didn't find a Generic+Specifc in the table, now we check
			// for Generic only
			snprintf(buffer, 10, "0x%02X", generic);

			// Check if we have a mapping in our map table
			if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
				nodeInfo->m_basicmapping = MapCommandClassBasic[buffer];
				return true;
			}
		}
	}
	return false;
}

void sigalrm_handler(int sig) {
	alarmset = false;
	Alarm currentAlarm = alarmlist.front();
	alarmlist.pop_front();
	if(atHome) {
		switch(s_mapStringTriggers[currentAlarm.description])
		{
			case Sunrise:
			{
				std::string morningScene;
				conf->GetMorningScene(morningScene);
				try {
					std::cout << activateScene(morningScene);
				}
				catch (ProtocolException& e) {
					string what = "ProtocolException: ";
					what += e.what();
					std::cout << what << endl;
					std::cout << "trigger went off, but no Scene is set for Sunrise" << endl;
				}
				break;
			}
			case Sunset:
			{
				std::string nightScene;
				conf->GetNightScene(nightScene);
				try {
					std::cout << activateScene(nightScene);
				}
				catch (ProtocolException& e) {
					string what = "ProtocolException: ";
					what += e.what();
					std::cout << what << endl;
					std::cout << "trigger went off, but no Scene is set for Sunset" << endl;
				}
				break;
			}
			default:
				// check if the description can be used as a Scene name
				// if that fails, check if the description can be parsed as a command
			break;
		}
	}
	
	// more alarms on the alarmlist? Set the next one (the list is already sorted...)
	
	time_t now = time(NULL);
	// remove alarms that are set in the past...
	while(!alarmlist.empty() && (alarmlist.front().alarmtime) <= now)
	{alarmlist.pop_front();}
					
	if(!alarmlist.empty() && !alarmset && (alarmlist.front().alarmtime > now)) {
		alarm((alarmlist.front().alarmtime - now));
		alarmset = true;
	}
}